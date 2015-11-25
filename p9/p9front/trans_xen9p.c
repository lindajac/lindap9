/*
 * The Xen 9p transport driver
 *
 *
 *  This is a prototype for a Xen 9p transport driver.  This file
 *  contains the interface to the 9p client code.
 *
 *  Copyright (C) 2015 Linda Jacobson
 *
 *  Copied with minor modifications from the virtio 9p transport driver
 *  Copyright (C) 2007, 2008 Eric Van Hensbergen, IBM Corporation
 *
 *  Based on virtio console driver
 *  Copyright (C) 2006, 2007 Rusty Russell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include "trans_common.h"
#include "p9.h"
#include "xen_9p_front.h"

/* a single mutex to manage channel initialization and attachment */
static DEFINE_MUTEX(xen_9p_lock);	// do these names have special meaning?
// static DECLARE_WAIT_QUEUE_HEAD(vp_wq); //do I need this?
//static atomic_t vp_pinned = ATOMIC_INIT(0); //don't think I need this.

static struct list_head xen9p_chan_list;

/* How many bytes left in this page. */
/*static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long) data % PAGE_SIZE);
	}*/

/**
 * p9_xen_close - reclaim resources of a channel
 * @client: client instance
 *
 * This reclaims a channel by freeing its resources and
 * reseting its inuse flag.
 * actually at the moment - no resources are freed - lrj
 * called in client.c and p9_front_driver.c
 *
 */

void p9_xen_close(struct p9_client *client)
{
	struct xen9p_chan *chan = client->trans;

	mutex_lock(&xen_9p_lock);
	if (chan)
		chan->inuse = false;
	mutex_unlock(&xen_9p_lock);
}

/**
 * req_done - called by handle response when server has completed request
 * @dataptr:  pointer to a buffer containing in this order:
 *            data sent to the server
 *            data sent fromt the server
 *
 */

void req_done(void *dataptr, struct xen9p_chan *chan, int16_t status,
	      uint16_t tag)
{
	struct p9_fcall *rc;
	unsigned int offset;
	struct p9_req_t *req;

	printk("request done\n");

	/*
	 *  calls functions in client.c that match requests to responses
	 *  and wake up the waiting requester
	 */
	req = p9_tag_lookup(chan->client, tag);
	offset = req->tc->size;
	rc = req->rc;
	memcpy (rc->sdata, dataptr+offset, rc->size);
	p9_client_cb(chan->client, req,  REQ_STATUS_RCVD);
}

/**pack_sg_list_p
 * pack_sg_list - pack a scatter gather list from a linear buffer
 * @sg: scatter/gather list to pack into
 * @start: which segment of the sg_list to start at
 * @limit: maximum segment to pack data to
 * @data: data to pack into scatter/gather list
 * @count: amount of data to pack into the scatter/gather list
 *
 * sg_lists have multiple segments of various sizes.  This will pack
 * arbitrary data into an existing scatter gather list, segmenting the
 * data as necessary within constraints.
 *
 */


/**
 * p9_xen_request - issue a request: use xen code to send request
 *               no sg list for now
 * @client: client instance issuing the request
 * @req: request to be issued
 *
 */

static int p9_xen_request(struct p9_client *client, struct p9_req_t *req)
{
	int err;
	//	int in, out, out_sgs, in_sgs;
	int out_len, in_len;
	// unsigned long flags;
	struct xen9p_chan *chan = client->trans;
	//	struct scatterlist *sgs[2];

	p9_debug(P9_DEBUG_TRANS, "9p debug: virtio request\n");
	req->status = REQ_STATUS_SENT;
	out_len = req->tc->size;
	in_len  = req->rc->capacity;
	/*
         * fyi all the metadata in the fcalls tc & rc is already in the data
         */
	err = p9front_handle_client_request (chan->drv_info,
					req->tc->tag,
					req->tc->sdata, out_len,
					req->rc->sdata, in_len);
	/* - no scatter gather or queues for now*/
	
   /*      req_retry:
	spin_lock_irqsave(&chan->lock, flags);

		out_sgs = in_sgs = 0;
			
	out = pack_sg_list(chan->sg, 0,
			   NUM_P9_SGLISTS, req->tc->sdata, req->tc->size);
	if (out)
		sgs[out_sgs++] = chan->sg;

	in = pack_sg_list(chan->sg, out,
			  NUM_P9_SGLISTS, req->rc->sdata,
			  req->rc->capacity);
	if (in)
		sgs[out_sgs + in_sgs++] = chan->sg + out;

	
	if (err < 0) {
		if (err == -ENOSPC) {
			chan->ring_bufs_avail = 0;
			spin_unlock_irqrestore(&chan->lock, flags);
			err = wait_event_interruptible(*chan->vc_wq,
						       chan->
						       ring_bufs_avail);
			if (err == -ERESTARTSYS)
				return err;

			p9_debug(P9_DEBUG_TRANS, "Retry virtio request\n");
			goto req_retry;
		} else {
			spin_unlock_irqrestore(&chan->lock, flags);
			p9_debug(P9_DEBUG_TRANS,
				 "virtio rpc add_sgs returned failure\n");
			return -EIO;
		}
	}
	virtqueue_kick(chan->vq);
	spin_unlock_irqrestore(&chan->lock, flags);

	p9_debug(P9_DEBUG_TRANS, "virtio request kicked\n");*/
	
	return 0;
}


/**
 * p9_xen_zc_request - issue a zero copy request
 * @client: client instance issuing the request
 * @req: request to be issued
 * @uidata: user bffer that should be ued for zero copy read
 * @uodata: user buffer that shoud be user for zero copy write
 * @inlen: read buffer size
 * @olen: write buffer size
 * @hdrlen: reader header size, This is the size of response protocol data
 *
 */
static int
p9_xen_zc_request(struct p9_client *client, struct p9_req_t *req,
		  char *uidata, char *uodata, int inlen,
		  int outlen, int in_hdr_len, int kern_buf)
{
  /*  	int in, out, err, out_sgs, in_sgs;
	unsigned long flags;
	int in_nr_pages = 0, out_nr_pages = 0;
	struct page **in_pages = NULL, **out_pages = NULL;
	struct xen9p_chan *chan = client->trans;
	struct scatterlist *sgs[4];*/

	p9_debug(P9_DEBUG_TRANS, "xen 9p zcrequest\n");
	p9_xen_request (client, req);
	return 0;  // this is on purpose - not implementing at the moment
	/*
	if (uodata) {
		out_nr_pages = p9_nr_pages(uodata, outlen);
		out_pages = kmalloc(sizeof(struct page *) * out_nr_pages,
				    GFP_NOFS);
		if (!out_pages) {
			err = -ENOMEM;
			goto err_out;
		}
		out_nr_pages = p9_get_mapped_pages(chan, out_pages, uodata,
						   out_nr_pages, 0,
						   kern_buf);
		if (out_nr_pages < 0) {
			err = out_nr_pages;
			kfree(out_pages);
			out_pages = NULL;
			goto err_out;
		}
	}
	if (uidata) {
		in_nr_pages = p9_nr_pages(uidata, inlen);
		in_pages = kmalloc(sizeof(struct page *) * in_nr_pages,
				   GFP_NOFS);
		if (!in_pages) {
			err = -ENOMEM;
			goto err_out;
		}
		in_nr_pages = p9_get_mapped_pages(chan, in_pages, uidata,
						  in_nr_pages, 1,
						  kern_buf);
		if (in_nr_pages < 0) {
			err = in_nr_pages;
			kfree(in_pages);
			in_pages = NULL;
			goto err_out;
		}
	}
	req->status = REQ_STATUS_SENT;
 req_retry_pinned:
	spin_lock_irqsave(&chan->lock, flags);

	out_sgs = in_sgs = 0;

	out = pack_sg_list(chan->sg, 0,
			   NUM_P9_SGLISTS, req->tc->sdata, req->tc->size);

	if (out)
		sgs[out_sgs++] = chan->sg;

	if (out_pages) {
		sgs[out_sgs++] = chan->sg + out;
		out += pack_sg_list_p(chan->sg, out, NUM_P9_SGLISTS,
				      out_pages, out_nr_pages, uodata,
				      outlen);
	}
	*/
	/*
	 * Take care of in data
	 * For example TREAD have 11.
	 * 11 is the read/write header = PDU Header(7) + IO Size (4).
	 * Arrange in such a way that server places header in the
	 * alloced memory and payload onto the user buffer.
	 */
	/*	in = pack_sg_list(chan->sg, out,
			  NUM_P9_SGLISTS, req->rc->sdata, in_hdr_len);
	if (in)
		sgs[out_sgs + in_sgs++] = chan->sg + out;

	if (in_pages) {
		sgs[out_sgs + in_sgs++] = chan->sg + out + in;
		in += pack_sg_list_p(chan->sg, out + in, NUM_P9_SGLISTS,
				     in_pages, in_nr_pages, uidata, inlen);
	}

	BUG_ON(out_sgs + in_sgs > ARRAY_SIZE(sgs));
	err = virtqueue_add_sgs(chan->vq, sgs, out_sgs, in_sgs, req->tc,
				GFP_ATOMIC);
	if (err < 0) {
		if (err == -ENOSPC) {
			chan->ring_bufs_avail = 0;
			spin_unlock_irqrestore(&chan->lock, flags);
			err = wait_event_interruptible(*chan->vc_wq,
						       chan->
						       ring_bufs_avail);
			if (err == -ERESTARTSYS)
				goto err_out;

			p9_debug(P9_DEBUG_TRANS, "Retry virtio request\n");
			goto req_retry_pinned;
		} else {
			spin_unlock_irqrestore(&chan->lock, flags);
			p9_debug(P9_DEBUG_TRANS,
				 "virtio rpc add_sgs returned failure\n");
			err = -EIO;
			goto err_out;
		}
	}
	virtqueue_kick(chan->vq);
	spin_unlock_irqrestore(&chan->lock, flags);
	p9_debug(P9_DEBUG_TRANS, "virtio request kicked\n");
	err = wait_event_interruptible(*req->wq,
				       req->status >= REQ_STATUS_RCVD);
	
	 * Non kernel buffers are pinned, unpin them
	 
	      err_out:
	if (!kern_buf) {
		if (in_pages) {
			p9_release_pages(in_pages, in_nr_pages);
			atomic_sub(in_nr_pages, &vp_pinned);
		}
		if (out_pages) {
			p9_release_pages(out_pages, out_nr_pages);
			atomic_sub(out_nr_pages, &vp_pinned);
		}
		 wakeup anybody waiting for slots to pin pages 
			wake_up(&vp_wq);
	}
	kfree(in_pages);
	kfree(out_pages);
	return err;*/
}

/**
 * p9_xen_create - initialize the transport; virtio uses a channel model, which
 *                 I'm copying
 * @client: client instance invoking this transport
 * @devname: string identifying the channel to connect to 
 * @args: args passed from sys_mount() for per-transport options (unused in this
 *        func - client.c has already parsed them, and stored relevant info in 
 *        p9_client struct
 *
 * This sets up a transport channel for 9p communication. 
 * Match the first available channel, using a simple reference count mechanism to ensure
 * that only a single mount has a channel open at a time.
 *
 */

static int
p9_xen_create(struct p9_client *client, const char *devname, char *args)
{
	struct xen9p_chan *chan;
	int ret = 0;
	int found = 0;

	mutex_lock(&xen_9p_lock);
	list_for_each_entry(chan, &xen9p_chan_list, chan_list) {
	  if (!strncmp(devname, chan->tag, chan->tag_len) &&
	      strlen(devname) == chan->tag_len) {
			if (!chan->inuse) {
				chan->inuse = true;
				found = 1;
				break;
			}
			ret = -EBUSY;
			goto out;
		}
	}
	mutex_unlock(&xen_9p_lock);

	if (!found) {
		pr_err("no channels available\n");
		ret = ENOENT;	
	} else {
		client->trans = (void *) chan;
		client->status = Connected;
		chan->client = client;
	}
 out:	
	return ret;
}

/*
 * not handling cancels at the moment
 */
static int p9_xen_cancel (struct p9_client *client, struct p9_req_t *req)
{
	return 1;
}

static struct p9_trans_module p9_xen_trans = {
	.name = "xen",
	.create = p9_xen_create,
	.close = p9_xen_close,
	.request = p9_xen_request,
	.zc_request = p9_xen_zc_request,
	.cancel = p9_xen_cancel,
	/*
	 * We leave one entry for input and one entry for response
	 * headers. We also skip one more entry to accomodate, address
	 * that are not at page boundary, that can result in an extra
	 * page in zero copy.
	 */
	.maxsize = PAGE_SIZE * (NUM_P9_SGLISTS - 3),
	.def = 0,
	.owner = THIS_MODULE,
};

/*
 * Called by the standard init function to initialize 9p data
*/
void init_xen_9p(void)
{
  printk(KERN_INFO "entering init_xen_9p\n");
	INIT_LIST_HEAD(&xen9p_chan_list);
	printk(KERN_INFO "just init chan_list\n");
	v9fs_register_trans(&p9_xen_trans);
	printk(KERN_INFO "just registered transport\n");
}

/*
 * ditto for cleanup
 */
void cleanup_xen_9p(void)
{
	v9fs_unregister_trans(&p9_xen_trans);
}
