/*  Xenbus Code for p9 frontend
    Copyright (C) 2015 Linda Jacobson
    Copyright (C) 2015 XenSource Ltd
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
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
#include "p9.h"

#define GRANT_INVALID_REF 0


struct grant {
    grant_ref_t gref;
    unsigned long pfn;
    struct list_head node;
};

static DEFINE_MUTEX(p9front_mutex);

struct p9_front_info {
	spinlock_t            io_lock;
	struct mutex          mutex;
	struct xenbus_device *xbdev;
	enum p9_state         connected;
	int                   ring_ref;
	struct p9_front_ring  ring;
        unsigned int          evtchn;
        unsigned int          irq;
	struct list_head      grants;
        char                 *page;
        p9_request_t          past_requests[10];  // magic # for now
	int is_ready;
};


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
	  gnttab_grant_foreign_access(info->xbdev->otherend_id, buffer_mfn, 0);
        return gnt_list_entry;
	out_of_memory:
printk (KERN_INFO "exiting get_grant error ENOMEM\n");
 ///FIX MEMEMEMME
 return (void *) -ENOMEM;
}

static void p9_free(struct p9_front_info *info, int suspend)
{
  printk (KERN_INFO "free");
	/* Prevent new requests being issued until we fix things up. */
	spin_lock_irq(&info->io_lock);
	info->connected = suspend ?
		P9_STATE_SUSPENDED : P9_STATE_DISCONNECTED;
	spin_unlock_irq(&info->io_lock);

	/* Free resources associated with old device channel. */
	if (info->ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->ring_ref, 0,
					  (unsigned long)info->ring.sring);
		info->ring_ref = GRANT_INVALID_REF;
		info->ring.sring = NULL;
	}
	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->evtchn = info->irq = 0;
 printk (KERN_INFO "exiting\n");
}

static void p9_handle_response (struct p9_response *bret, struct p9_front_info *info)
{
    unsigned long id;
    p9_request_t *req_ptr;
    int error;

    id   = bret->id;
    printk ("in handle_response; id is %lu\n", id);
    error = (bret->status == P9_RSP_OKAY) ? 0 : -EIO;
    if (error) BUG();

    if (bret->operation == P9_OP_READ) {
      char astr[11];
      req_ptr = &(info->past_requests[id]);
      strncpy (astr, info->page + req_ptr->offset, req_ptr->nrbytes);
      astr[req_ptr->nrbytes] = '\0';
      printk ("front-end read %s, for %d bytes\n", astr, req_ptr->nrbytes);
    } else {  //OP_WRITE
      printk ("write op completed\n");
    }
}
    
static irqreturn_t p9_interrupt(int irq, void *dev_id)
{

	struct p9_response *bret;
	RING_IDX i, rp;
	unsigned long flags;
	struct p9_front_info *info = (struct p9_front_info *)dev_id;

  printk (KERN_INFO "interrupt\n");
        spin_lock_irqsave(&info->io_lock, flags);

 again:
	rp = info->ring.sring->rsp_prod;
	rmb(); /* Ensure we see queued responses up to 'rp'. */

	for (i = info->ring.rsp_cons; i != rp; i++) {
		bret = RING_GET_RESPONSE(&info->ring, i);
  	        p9_handle_response (bret, info);
	}

// moving consumer ring pointer
	info->ring.rsp_cons = i;

	if (i != info->ring.req_prod_pvt) {
		int more_to_do;
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
		if (more_to_do)	  {
		  //I shouldn't be here
		  printk (KERN_INFO "yikes i is %d; info->ring.req_prod_pvt is %d\n", i, info->ring.req_prod_pvt);
			goto again;
		}
	} else
		info->ring.sring->rsp_event = i + 1;


	spin_unlock_irqrestore(&info->io_lock, flags);
	return IRQ_HANDLED;
}

static int p9front_is_ready (struct xenbus_device *dev)
{
    struct p9_front_info *info; 
 printk (KERN_INFO "isready");
    info  = dev_get_drvdata (&dev->dev);
 printk (KERN_INFO "exiting\n");
  return info->is_ready;
}  

static int setup_9p_ring(struct xenbus_device *dev,
			 struct p9_front_info *info)
{
	struct p9_sring *sring;
	int err;

	info->ring_ref = GRANT_INVALID_REF;
	sring = (struct p9_sring *)__get_free_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		 printk (KERN_INFO "exiting enomem\n");
		return -ENOMEM;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&info->ring, sring, PAGE_SIZE);
	err = xenbus_grant_ring(dev, virt_to_mfn(info->ring.sring));
	if (err < 0) {
		free_page((unsigned long)sring);
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
	printk (KERN_INFO "exiting setup_p9_ring at fail\n");
	p9_free(info, 0);
	return err;
}

/* Common code used when first setting up, and when resuming. */
static int talk_to_9p_back(struct xenbus_device *dev,
			   struct p9_front_info *info)
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
	 printk (KERN_INFO "aborting talk to 9p back\n");
 destroy_p9ring:
        printk (KERN_INFO "cleaningup t t 9pback \n");
	p9_free(info, 0);
 out:
	return err;
}

/**
 * Entry point to this code when ???.  Allocate the basic
 * structures and the ring buffer for communication with the backend, and
 * inform the backend of the appropriate details for those.  Switch to
 * Initialised state.
 */
static int p9front_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
      struct p9_front_info *info;
      int  err;
 
      info = kzalloc (sizeof (*info), GFP_KERNEL);
      if (!info) {
	xenbus_dev_fatal (dev, -ENOMEM, "allocating info struct");
	return -ENOMEM;
      }
      spin_lock_init(&info->io_lock);
      info->xbdev = dev;
      info->connected = P9_STATE_DISCONNECTED;
	/* Front end dir is a number, which is used as the id. */
      dev_set_drvdata(&dev->dev, info);
      
      err = talk_to_9p_back (dev,info);
      if (err) {
	kfree (info);
	dev_set_drvdata (&dev->dev, NULL);
	 printk (KERN_INFO "exiting err\n");
	return (err);
      }
      return 0;
}

/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  Not sure what p9 needs to do yet
 */
static int p9front_resume(struct xenbus_device *dev)
{
  printk (KERN_INFO "resume");	
	return 0;
}

static void
p9front_closing(struct p9_front_info *info)
{
	struct xenbus_device *xbdev = info->xbdev;
  printk (KERN_INFO "closing");
	xenbus_frontend_closed(xbdev);
 printk (KERN_INFO "exiting\n");
}
/*
 * temporary strings for testing
 */
const char *tstrs[] = {"s 1", "s 2", "s 3", "s4", "s5\n", "s6", "s 7", "s 8", "s 9", "s 10",};

/*
 * Invoked when the backend is finally 'ready' 
 */
static void p9front_connect(struct p9_front_info *info)
{
        struct page  *apage;
	char         *addr;
	int           i;
	p9_request_t *ring_req;
	struct grant *gnt_list_entry = NULL;
	int           offset = 0;

 	spin_lock_irq(&info->io_lock);
	xenbus_switch_state(info->xbdev, XenbusStateConnected);
	info->connected = P9_STATE_CONNECTED;
	info->is_ready = 1;
	spin_unlock_irq(&info->io_lock);
	/*
	 * send multiple pieces of data - to test no delays
	 */

	apage = alloc_page(GFP_NOIO);
	addr = (char *)page_address (apage);
	info->page = addr;
        gnt_list_entry = get_grant((unsigned long) page_to_pfn(apage), info);
	info->ring.req_prod_pvt = 0;

	for (i = 0; i < 10; i++) {
	    /*
             * Fill out a communications ring structure. 
             */
            ring_req = RING_GET_REQUEST(&info->ring, info->ring.req_prod_pvt);
            ring_req->id = i;
	    ring_req->gref = gnt_list_entry->gref;
      	    ring_req->offset = offset;

	    if ((i % 2) == 0) {
	        /*  
                 * just want to test 2 things:  Reads and writes
                 * and whether I'm computing offsets in page correctly.
                 */
	        ring_req->nrbytes = strlen(tstrs[i]) + 1;
	        ring_req->operation = P9_OP_WRITE;
		/*
                 *  generate data in shared page
                 */
		strcpy (addr, tstrs[i]);
  	    }
	    else {
	         ring_req->operation = P9_OP_READ;
	         ring_req->nrbytes =  i + 1;  // want # between 1 and 10
	    }
	    offset += ring_req->nrbytes;
	    addr += ring_req->nrbytes;
	    /*
             * save above data - I think there's a simpler way
             */
	    memcpy (&(info->past_requests[i]), ring_req, sizeof (p9_request_t));
 
	    /*
	     * I wanted to send some notifications, 1 at a time, and some in a
             * batch below.  These 5 gto sent twice.
             
	    if (i < 5) {
	      RING_PUSH_REQUESTS (&info->ring);
	      notify_remote_via_irq (info->irq);
            }*/
       	    info->ring.req_prod_pvt++;
    	} 
        /*
         *  Now push the request and notify the other side
         * for this test - I want some notifications to be separate
         * and some to be batched
         */

	RING_PUSH_REQUESTS (&info->ring);
        notify_remote_via_irq (info->irq);
	return;
}

/**
 * Callback received when the backend's state changes.
 */
static void p9back_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
	struct p9_front_info *info = dev_get_drvdata(&dev->dev);

  printk (KERN_INFO "back_changed");
	dev_dbg(&dev->dev, "p9front:p9back_changed to state %d.\n", backend_state);

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitWait:
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
		break;

	case XenbusStateConnected:
		p9front_connect(info);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		/* Missed the backend's Closing state -- fallthrough */
	case XenbusStateClosing:
		p9front_closing(info);
		break;
	}
 printk (KERN_INFO "exiting\n");
}

static int p9front_remove(struct xenbus_device *xbdev)
{
	struct p9_front_info *info = dev_get_drvdata(&xbdev->dev);

  printk (KERN_INFO "remove");
        dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

	p9_free(info, 0);

	info->xbdev = NULL;
	kfree(info);
	 printk (KERN_INFO "exiting\n");
	return 0;
}

static const struct xenbus_device_id p9front_ids[] = {
	{ "p9" },
	{ "" }
};

static struct xenbus_driver p9front_driver = {
        .ids = p9front_ids,
	.probe = p9front_probe,
	.remove = p9front_remove,
	.resume = p9front_resume,
	.otherend_changed = p9back_changed,
	.is_ready = p9front_is_ready,
};

static int __init xlp9_init(void)
{
	int ret = 0;
  printk (KERN_INFO "\n\n in p9_init\n");
        p9front_driver.driver.name = "p9";
	p9front_driver.driver.owner = THIS_MODULE;
	ret = xenbus_register_frontend(&p9front_driver);
 printk (KERN_INFO "exiting p9_init\n");
	return ret;
}
module_init(xlp9_init);


static void __exit xlp9_exit(void)
{
    printk (KERN_INFO "exit");
	xenbus_unregister_driver(&p9front_driver);
	 printk (KERN_INFO "exiting\n");
//	kfree(minors);
}
module_exit(xlp9_exit);

MODULE_DESCRIPTION("Xen virtual 9P frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:p9");
MODULE_ALIAS("xenp9");
MODULE_AUTHOR("Linda Jacobson");
