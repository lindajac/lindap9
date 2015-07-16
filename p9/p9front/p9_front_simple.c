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

struct p9_front_info
{
	spinlock_t io_lock;
	struct mutex mutex;
	struct xenbus_device *xbdev;
	enum p9_state connected;
	int ring_ref;
	struct p9_front_ring ring;
	unsigned int evtchn, irq;
	struct gnttab_free_callback callback;
	struct list_head grants;
	int is_ready;
	int test_val;
};

int data_xfer [2048];

/*static int fill_grant_buffer(struct p9_front_info *info)
{
       struct grant *gnt_list_entry;

         printk (KERN_INFO "fillgrantbuffer");
	 
	gnt_list_entry = kzalloc(sizeof(struct grant), GFP_NOIO);
	if (!gnt_list_entry)
		goto out_of_memory;

	gnt_list_entry->gref = GRANT_INVALID_REF;
	list_add(&gnt_list_entry->node, &info->grants);
  printk (KERN_INFO "exiting\n");
	return 0;
out_of_memory:
        printk (KERN_INFO "exiting error ENOMEM\n");
	return -ENOMEM;
  
	}*/


static struct grant *get_grant(grant_ref_t *gref_head,
                               unsigned long pfn,
                               struct p9_front_info *info)
{
	struct grant *gnt_list_entry;
	unsigned long buffer_mfn;

  printk (KERN_INFO "getgrant");
        BUG_ON(list_empty(&info->grants));
	/*	gnt_list_entry = list_first_entry(&info->grants, struct grant,
	                                  node);
	list_del(&gnt_list_entry->node);

	if (gnt_list_entry->gref != GRANT_INVALID_REF) {
 printk (KERN_INFO "exiting1\n");
	  return gnt_list_entry;
	  }*/
	gnt_list_entry = kzalloc(sizeof(struct grant), GFP_NOIO);
	if (!gnt_list_entry)
		goto out_of_memory;

	/* Assign a gref to this page */
	gnt_list_entry->gref = gnttab_claim_grant_reference(gref_head);
		BUG_ON(gnt_list_entry->gref == -ENOSPC);
	gnt_list_entry->pfn = pfn;

	buffer_mfn = pfn_to_mfn(gnt_list_entry->pfn);
	gnttab_grant_foreign_access_ref(gnt_list_entry->gref,
	                                info->xbdev->otherend_id,
	                                buffer_mfn, 0);
 printk (KERN_INFO "exiting normal\n");
        return gnt_list_entry;
	out_of_memory:
 printk (KERN_INFO "exiting error ENOMEM\n");
 ///FIX MEMEMEMME
	return -ENOMEM;
}


static void p9_free(struct p9_front_info *info, int suspend)
{
  printk (KERN_INFO "free");
	/* Prevent new requests being issued until we fix things up. */
	spin_lock_irq(&info->io_lock);
	info->connected = suspend ?
		P9_STATE_SUSPENDED : P9_STATE_DISCONNECTED;
	/* No more gnttab callback work. */
	gnttab_cancel_free_callback(&info->callback);
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

static irqreturn_t p9_interrupt(int irq, void *dev_id)
{

	struct p9_response *bret;
	RING_IDX i, rp;
	unsigned long flags;
	struct p9_front_info *info = (struct p9_front_info *)dev_id;
	int error;

  printk (KERN_INFO "interrupt");
        spin_lock_irqsave(&info->io_lock, flags);

 again:
	rp = info->ring.sring->rsp_prod;
//	rmb(); /* Ensure we see queued responses up to 'rp'. */

	for (i = info->ring.rsp_cons; i != rp; i++) {
		unsigned long id;

		bret = RING_GET_RESPONSE(&info->ring, i);
		id   = bret->id;
	
		error = (bret->status == P9_RSP_OKAY) ? 0 : -EIO;
		if (error) BUG();
	}

// moving consumer ring pointer
	info->ring.rsp_cons = i;

	if (i != info->ring.req_prod_pvt) {
		int more_to_do;
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
		if (more_to_do)	  {
		  //I shouldn't be here
		   printk (KERN_INFO "yikes\n");
			goto again;
		}
	} else
		info->ring.sring->rsp_event = i + 1;


	spin_unlock_irqrestore(&info->io_lock, flags);
 printk (KERN_INFO "exiting\n");
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

  printk (KERN_INFO "setup9pring");
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
	printk (KERN_INFO "ring ref is %d \n",info->ring_ref);
	err = xenbus_alloc_evtchn(dev, &info->evtchn);
	if (err)
		goto fail;
	printk (KERN_INFO "evtchn is %d \n",info->evtchn);

	err = bind_evtchn_to_irqhandler(info->evtchn, p9_interrupt, 0,
					"p9", info);
	if (err <= 0) {
		xenbus_dev_fatal(dev, err,
				 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}
	info->irq = err;
	printk (KERN_INFO "info->irq is %d \n",info->irq);

 printk (KERN_INFO "exiting\n");
        return 0;
fail:
	printk (KERN_INFO "exiting at fail\n");
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

  printk (KERN_INFO "talkto9pback");
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
 printk (KERN_INFO "exiting\n");
	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, err, "%s", message);
	 printk (KERN_INFO "aborting\n");
 destroy_p9ring:
	  printk (KERN_INFO "cleaningup \n");
	p9_free(info, 0);
 out:
	 printk (KERN_INFO "exiting\n");
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
  printk (KERN_INFO "probe");
      info = kzalloc (sizeof (*info), GFP_KERNEL);
      if (!info) {
	xenbus_dev_fatal (dev, -ENOMEM, "allocating info struct");
 printk (KERN_INFO "exiting enomem\n");
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
       printk (KERN_INFO "exiting\n");
      return 0;
}


/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  We tear down our blkif structure and recreate it
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
 * Invoked when the backend is finally 'ready' 
 */
static void p9front_connect(struct p9_front_info *info)
{
        grant_ref_t gref_head;
	//	struct page *apage;
	struct xenbus_transaction xbt;
	int err;
	const char *message = NULL;
	struct grant *gnt_list_entry = NULL;
	
  printk (KERN_INFO "connect");
  /*err = fill_grant_buffer(info);
	if (err) {
 printk (KERN_INFO "exiting err\n");
	        xenbus_dev_fatal(info->xbdev, err, "fill_grant_buffer %s",
				 info->xbdev->otherend);
		return;
		}*/
	spin_lock_irq(&info->io_lock);
	xenbus_switch_state(info->xbdev, XenbusStateConnected);
	info->connected = P9_STATE_CONNECTED;
	info->is_ready = 1;
	/*
	 * send first piece of data - to test
	 */
	data_xfer[0] = 5;
	
        gnt_list_entry = get_grant(&gref_head, (unsigned long) virt_to_pfn(data_xfer), info);

	err = xenbus_transaction_start(&xbt);
	if (err) {
	   printk (KERN_INFO "exiting err2\n");
		xenbus_dev_fatal(info->xbdev, err, "starting transaction");
		goto out;
	}
        err = xenbus_printf(xbt, info->xbdev->nodename,
			    "gref", "%u", gnt_list_entry->gref);
	if (err) {
		message = "writing gref";
		goto abort_transaction;
	}
	xenbus_transaction_end(xbt, 0);
	spin_unlock_irq(&info->io_lock);
	return ;
abort_transaction:
	 printk (KERN_INFO "exiting abort\n");
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(info->xbdev, err, "%s", message);
out:
	 printk (KERN_INFO "exiting\n");
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
