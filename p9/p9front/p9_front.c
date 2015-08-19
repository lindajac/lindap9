/*
 *   Code for handling communication between Xen 9p front and back ends
 *
 *   Copyright (C) 2015 Linda Jacobson
 *   Copyright (C) 2015 XenSource Ltd
 *  
 *  based on xen_blkfront.c code the front end of the Xen block driver
 *
 *
 *
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 * Copyright (c) 2004, Christian Limpach
 * Copyright (c) 2004, Andrew Warfield
 * Copyright (c) 2005, Christopher Clark
 * Copyright (c) 2005, XenSource Ltd
 ** blkfront.c
 *
 * XenLinux virtual block device driver.
 *
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 * Copyright (c) 2004, Christian Limpach
 * Copyright (c) 2004, Andrew Warfield
 * Copyright (c) 2005, Christopher Clark
 * Copyright (c) 2005, XenSource Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
*/

#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/cdrom.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/bitmap.h>
#include <linux/list.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/platform_pci.h>

#include <xen/interface/grant_table.h>
#include <xen/interface/io/protocols.h>

#include <asm/xen/hypervisor.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include "p9.h"
#include "xen_9p_front.h"

#define GRANT_INVALID_REF 0

/*
 * list of free and used ids
 */
static int used_id[PAGE_SIZE];

static DEFINE_MUTEX(p9front_mutex);

static int  get_id_from_freelist (void)
{
	int i;

	for (i=0; i<PAGE_SIZE; i++) 
		if (!used_id[i])
			return i;
	return -1;
}

static void init_freelist (void)
{
	int i;

	for (i=0; i<PAGE_SIZE; i++) 
		used_id[i] = false;
}

static struct grant *get_grant(unsigned long pfn,
			       struct p9_front_info *info)
{
	struct grant *gnt_list_entry;
	unsigned long buffer_mfn;

	gnt_list_entry = kzalloc(sizeof(struct grant), GFP_NOIO);
	if (!gnt_list_entry)
		goto out_of_memory;

	gnt_list_entry->pfn = pfn;

	buffer_mfn = pfn_to_mfn(pfn);

	/* Assign a gref to this page */
	gnt_list_entry->gref =
	    gnttab_grant_foreign_access(info->xbdev->otherend_id,
					buffer_mfn, 0);
	return gnt_list_entry;
      out_of_memory:
	printk(KERN_INFO "exiting get_grant error ENOMEM\n");
	///FIX MEMEMEMME
	return (void *) -ENOMEM;
}

/*
 * called whenever it's necessary to free up resources:  suspend/resume, exiting
 */
void p9_free(struct p9_front_info *info, int suspend)
{
	printk(KERN_INFO "free");
	/* Prevent new requests being issued until we fix things up. */
	spin_lock_irq(&info->io_lock);
	info->connected = suspend ?
	    P9_STATE_SUSPENDED : P9_STATE_DISCONNECTED;
	spin_unlock_irq(&info->io_lock);

	/* Free resources associated with old device channel. */
	if (info->ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->ring_ref, 0,
					  (unsigned long) info->ring.
					  sring);
		info->ring_ref = GRANT_INVALID_REF;
		info->ring.sring = NULL;
	}
	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->evtchn = info->irq = 0;
	printk(KERN_INFO "exiting\n");
}
/*
 * p9_handle_response:  get request corresponding to response
 *                      pass this info + data to req_done in trans_xen9p.c
 *
 *  @bret -  the response struct
 *  @info -  per device information including the array of past requests
 *
 */
void p9_handle_response(struct p9_response *bret,
			       struct p9_front_info *info)
{
	unsigned long id;
	void *addr;

	id = bret->id;
	printk("in handle_response; id is %lu\n", id);
	used_id[id] = false;
	addr = &(info->addresses[id]);
	req_done (addr, info->chan, bret->status, bret->tag);
}

static irqreturn_t p9_interrupt(int irq, void *dev_id)
{

	struct p9_response *bret;
	RING_IDX i, rp;
	unsigned long flags;
	struct p9_front_info *info = (struct p9_front_info *) dev_id;

	printk(KERN_INFO "interrupt\n");
	spin_lock_irqsave(&info->io_lock, flags);

      again:
	rp = info->ring.sring->rsp_prod;
	rmb();			/* Ensure we see queued responses up to 'rp'. */

	for (i = info->ring.rsp_cons; i != rp; i++) {
		bret = RING_GET_RESPONSE(&info->ring, i);
		p9_handle_response(bret, info);
	}

// moving consumer ring pointer
	info->ring.rsp_cons = i;

	if (i != info->ring.req_prod_pvt) {
		int more_to_do;
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
		if (more_to_do) {
			//I shouldn't be here
			printk(KERN_INFO
			       "yikes i is %d; info->ring.req_prod_pvt is %d\n",
			       i, info->ring.req_prod_pvt);
			goto again;
		}
	} else
		info->ring.sring->rsp_event = i + 1;


	spin_unlock_irqrestore(&info->io_lock, flags);
	return IRQ_HANDLED;
}

/*
 * setup_9p_ring - call RING macros to initalize xen ring
 *
 * @dev - the device information
 * @info - the per instance info
 *
 */
static int setup_9p_ring(struct xenbus_device *dev,
			 struct p9_front_info *info)
{
	struct p9_sring *sring;
	int err;

	info->ring_ref = GRANT_INVALID_REF;
	sring = (struct p9_sring *) __get_free_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		printk(KERN_INFO "exiting enomem\n");
		return -ENOMEM;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&info->ring, sring, PAGE_SIZE);
	err = xenbus_grant_ring(dev, virt_to_mfn(info->ring.sring));
	if (err < 0) {
		free_page((unsigned long) sring);
		info->ring.sring = NULL;
		goto fail;
	}
	info->ring_ref = err;
	err = xenbus_alloc_evtchn(dev, &info->evtchn);
	if (err)
		goto fail;
	err = bind_evtchn_to_irqhandler(info->evtchn, p9_interrupt, 0,
					"p9", info);
	if (err <= 0) {
		xenbus_dev_fatal(dev, err,
				 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}
	info->irq = err;
	return 0;
      fail:
	printk(KERN_INFO "exiting setup_p9_ring at fail\n");
	p9_free(info, 0);
	return err;
}

/* Common code used when first setting up, and when resuming. */
int talk_to_9p_back(struct xenbus_device *dev, struct p9_front_info *info)
{
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int err;

/* Create shared ring, alloc event channel. */
	err = setup_9p_ring(dev, info);
	if (err)
		goto out;
      again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_p9ring;
	}

	err = xenbus_printf(xbt, dev->nodename,
			    "ring-ref", "%u", info->ring_ref);
	if (err) {
		message = "writing ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename,
			    "event-channel", "%u", info->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}


	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_p9ring;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);
	return 0;

      abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, err, "%s", message);
	printk(KERN_INFO "aborting talk to 9p back\n");
      destroy_p9ring:
	printk(KERN_INFO "cleaningup t t 9pback \n");
	p9_free(info, 0);
      out:
	return err;
}

void p9front_closing(struct p9_front_info *info)
{
	struct xenbus_device *xbdev = info->xbdev;
	printk(KERN_INFO "closing");
	xenbus_frontend_closed(xbdev);
	printk(KERN_INFO "exiting\n");
}

int p9front_handle_client_request (struct p9_front_info *info,
					uint16_t tag,
					char *out_data, int out_len,
					char *in_data, int in_len)
{
	int err = 0;
	struct page *apage;
	char *addr;
	int tot_sz;
	p9_request_t *ring_req;
	struct grant *gnt_list_entry = NULL;
	int offset = 0;
	int id;

	if (!info->is_ready) {
		err = -1;  
		/* wait */goto out;
	}  
	tot_sz = out_len + in_len;
	if (tot_sz > PAGE_SIZE) {
	  	printk ("request too large: out_len is %u and in_len is %u",
			out_len, in_len);
		err = -ENOSPC;
		goto out;
	}
	if (tot_sz + info->offset > PAGE_SIZE) {
		apage = alloc_page(GFP_NOIO);
		if (tot_sz < info->offset)	// just temporary
			info->page = apage;
		info->offset = 0;
	} else {
		apage = info->page;
		offset = info->offset;
	}
	addr = (char *) page_address(apage) + offset;
	gnt_list_entry =
	    get_grant((unsigned long) page_to_pfn(apage), info);
	ring_req = RING_GET_REQUEST(&info->ring, info->ring.req_prod_pvt);
	/*
	 * FIX - will need to test for bad id when using multiple pages
	 */
	id = get_id_from_freelist ();
	ring_req->id = id;
	ring_req->gref = gnt_list_entry->gref;
	ring_req->offset = info->offset;
	ring_req->nrbytes = tot_sz;
	ring_req->out_len = out_len;
	ring_req->in_len = in_len;
	ring_req->tag = tag;
	
	info->offset += ring_req->nrbytes;
	memcpy (addr, out_data, out_len);
	addr += out_len;
	/*
	 * save where to start looking for the input
	 */
        info->addresses[id] = addr;

	info->ring.req_prod_pvt++;
	/*
	 *  Now push the request and notify the other side
	 */
	RING_PUSH_REQUESTS(&info->ring);
	notify_remote_via_irq(info->irq);
 out:
	return (err);
}

/*
 * Invoked when the backend is finally 'ready' 
 */

void p9front_connect(struct p9_front_info *info)
{
	spin_lock_irq(&info->io_lock);
	xenbus_switch_state(info->xbdev, XenbusStateConnected);
	info->connected = P9_STATE_CONNECTED;
	info->is_ready = 1;
	init_freelist ();
	spin_unlock_irq(&info->io_lock);
	return;
}
