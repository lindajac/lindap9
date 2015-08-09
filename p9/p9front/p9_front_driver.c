/*
 * The Xen 9p transport driver
 *
 *
 *  This is a prototype for a Xen 9p transport driver. 
 *  This file contains the xenbus driver portions of the code:
 *           the functions defined in a struct xenbus_driver.
 *  The Xen ring interface is in the file p9_front.c
 *  The interface to the 9p client is in trans_xen9p.c
 *
 *  Copyright (C) 2015 Linda Jacobson
 *
 * Based on similar code in  blkfront.c:  XenLinux virtual block device driver.
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 * Copyright (c) 2004, Christian Limpach
 * Copyright (c) 2004, Andrew Warfield
 * Copyright (c) 2005, Christopher Clark
 * Copyright (c) 2005, XenSource Ltd
 *  
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
#include "trans_common.h"
#include "p9.h"
#include "xen_9p_front.h"

static struct list_head xen9p_chan_list;
static DEFINE_MUTEX(xen_9p_lock);

/**
 * p9_xen_probe - probe for existence of 9P channels
 *                initialize 9p "device"
 *
 * @xbdev: xen device to probe
 *
 * Entry point to this code when a new "device" is created.  Allocate the basic
 * structures and the ring buffer for communication with the backend, and
 * inform the backend of the appropriate details for those.  Switch to
 * Initialised state.
 */
static int p9_xen_probe(struct xenbus_device *dev,
			const struct xenbus_device_id *id)
{
	struct p9_front_info *info;
	int    tag_len;
	char   *tag;
	struct xen9p_chan *chan;
	int    err;


	err = xenbus_scanf (XBT_NIL, dev->nodename,
				    "mount_tag_len","%i", &tag_len);
	if (err) {
		tag = kzalloc(tag_len, GFP_KERNEL);
		if (!tag) {
			err = -ENOMEM;
			goto fail;
		}	  
	} else {
  		/*
		* tag_len field is initialized to 0; no mount tag present, yet
		*/
		err = -EINVAL;
		goto fail;
	 }

	/*
	 * allocate the channel;  This is data used when processing 
	 * 9p client requests
	 */
	chan = kzalloc(sizeof(struct xen9p_chan), GFP_KERNEL);
	if (!chan) {
		pr_err("Failed to allocate virtio 9P channel\n");
		err = -ENOMEM;
		goto out_free_tag;
	}
	chan->tag = tag;
	chan->tag_len = tag_len;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info struct");
		err = -ENOMEM;
		goto out_free_chan;
	}
	spin_lock_init(&info->io_lock);
	info->xbdev = dev;
	info->connected = P9_STATE_DISCONNECTED;
	info->chan = chan;
	chan->drv_info = info;
	/* Front end dir is a number, which is used as the id. */
	dev_set_drvdata(&dev->dev, info);
	/*
	 * initialize ring, event chan, etc.
	 */
	err = talk_to_9p_back(dev, info);
	if (err) {
		goto xen_err;
	}
	/*
	 * init scatter gather list
	 */
	sg_init_table(chan->sg, NUM_P9_SGLISTS);
	/*  not sure what this is for, but saving just in case.
	 err = sysfs_create_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	 if (err) {
	 goto out_free_tag;
	 }
	 */
	chan->vc_wq = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL);
	if (!chan->vc_wq) {
		err = -ENOMEM;
		goto out_free_tag;
	}
	init_waitqueue_head(chan->vc_wq);

	/* Ceiling limit to avoid denial of service attacks
	 * need to figure out what it should be
	 */
	chan->p9_max_pages = 37; /* virtio called nr_free_buffer_pages */

	mutex_lock(&xen_9p_lock);
	list_add_tail(&chan->chan_list, &xen9p_chan_list);
	mutex_unlock(&xen_9p_lock);
	return 0;

xen_err:	
	kfree(info);
	dev_set_drvdata(&dev->dev, NULL);
	printk(KERN_INFO "exiting xen err\n");
out_free_chan:
	kfree(chan);	
out_free_tag:
	kfree(tag);
fail:
	return err;
}


/**
 * Callback received when the backend's state changes.
 *
 */
static void p9_xen_back_changed(struct xenbus_device *dev,
				enum xenbus_state backend_state)
{
	struct p9_front_info *info = dev_get_drvdata(&dev->dev);

	printk(KERN_INFO "back_changed");
	dev_dbg(&dev->dev, "p9front:p9back_changed to state %d.\n",
		backend_state);

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
	printk(KERN_INFO "exiting\n");
}

/*
 * p9_xen_resume - callback when we reconnect to backend due to suspend/resume
 *                 or backend driver restart.
 * @xbdev: xen device to probe
 *
 * We tear down our info structure and recreate it, but
 * leave the device-layer structures intact so that this is transparent to the
 * rest of the kernel.
 */
static int p9_xen_resume(struct xenbus_device *dev)
{
	struct p9_front_info *info = dev_get_drvdata(&dev->dev);
	int err;

	printk(KERN_INFO "resume");
	dev_dbg(&dev->dev, "blkfront_resume: %s\n", dev->nodename);

	p9_free(info, info->connected == P9_STATE_CONNECTED);

	/*
	 * talk_to_9p_back will also set the front end state to Initialized
	 */
	err = talk_to_9p_back(dev, info);

	/*
	 * We have to wait for the backend to switch to
	 * connected state, since we want to read which
	 * features it supports.
	 */

	return err;
}

/*
 *
 * p9_xen_remove - clean up resources associated with 9p "device"
 * @xbdev: xen device to remove
 *
 */
static int p9_xen_remove(struct xenbus_device *xbdev)
{
	struct p9_front_info *info = dev_get_drvdata(&xbdev->dev);
	struct xen9p_chan *chan = info->chan;

	if (chan->inuse)
		p9_xen_close(chan->client);

	printk(KERN_INFO "remove");
	dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

	/*
	 * frees up xen specific data
	 */
	p9_free(info, 0);
	mutex_lock(&xen_9p_lock);
	list_del(&chan->chan_list);
	mutex_unlock(&xen_9p_lock);
	/*      sysfs_remove_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	   kfree(chan->tag);
FIX - check correct trans_virtio - for how to clean up channels.
 */

	kfree(chan->vc_wq);
	kfree(chan);
	info->xbdev = NULL;
	/* 
	 *  CHECK (FIX ME?) Do I need to free other fields of info
	 */
	kfree(info);
	printk(KERN_INFO "exiting\n");
	return 0;
}

/*static struct virtio_device_id id_table[] = {              not sure if I need this - don't think so.
	{ VIRTIO_ID_9P, VIRTIO_DEV_ANY_ID },
	{ 0 },
	}; 

static unsigned int features[] = {
	VIRTIO_9P_MOUNT_TAG,
	};*/


static const struct xenbus_device_id p9front_ids[] = {
	{"p9"},
	{""}
};

/*
 * standard xenbus driver struct
 */
static struct xenbus_driver p9front_driver = {
	.ids = p9front_ids,
	/*      .feature_table      = features,
	   .feature_table_size = ARRAY_SIZE(features), */
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.probe = p9_xen_probe,
	.remove = p9_xen_remove,
	.resume = p9_xen_resume,
	.otherend_changed = p9_xen_back_changed,
};

/* The standard init function */
static int __init xlp9_init(void)
{
	int ret = 0;

	init_xen_9p();

	printk(KERN_INFO "\n\n in p9_init\n");
	p9front_driver.driver.name = "p9";
	p9front_driver.driver.owner = THIS_MODULE;
	ret = xenbus_register_frontend(&p9front_driver);
	printk(KERN_INFO "exiting p9_init\n");
	return ret;
}

module_init(xlp9_init);

/*  The standard module exit function */
static void __exit xlp9_exit(void)
{
	printk(KERN_INFO "exit");
	xenbus_unregister_driver(&p9front_driver);
	cleanup_xen_9p ();
	printk(KERN_INFO "exiting\n");
}

module_exit(xlp9_exit);

MODULE_DESCRIPTION("Xen virtual 9P frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:p9");
MODULE_ALIAS("xenp9");
MODULE_AUTHOR("Linda Jacobson");
