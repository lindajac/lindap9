/*
 * The Xen 9p transport driver
 *
 *  This is a prototype for a Xen 9p transport driver.  
 *  This header file contains definitions common to the front end of this transport
 *     There are 3 .c files for the front end, that share this header file:
 *          p9_front.c that manages the xen communication with the backend
 *          p9_front_driver.c that provides the driver function interface (init, probe, resume, etc.)
 *          trans_xen9p.c that provides the 9p client interface
 *  
 *  Copyright (C) 2015 Linda Jacobson
 *
 * This is a block based transport driver based on the lguest block driver
 * code. 
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
#define NUM_P9_SGLISTS	128

/* a single mutex to manage channel initialization and attachment */
static DEFINE_MUTEX(xen_9p_lock);  // do these names have special meaning?
static DECLARE_WAIT_QUEUE_HEAD(vp_wq);
static atomic_t vp_pinned = ATOMIC_INIT(0);

/**
 * struct xen9p_chan - per-instance transport information
 * @initialized: whether the channel is initialized
 * @inuse: whether the channel is in use
 * @lock: protects multiple elements within this structure
 * @client: client instance
 * @vdev: virtio dev associated with this channel
 * @vq: virtio queue associated with this channel
 * @sg: scatter gather list which is used to pack a request (protected?)
 *
 * We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure needs to be put in the transport private.
 *
 */

struct xen9p_chan {
	bool inuse;

	spinlock_t lock;

	struct p9_client *client;
  //  CHANGE
	struct virtio_device *vdev;
	struct virtqueue *vq;

	wait_queue_head_t *vc_wq;
  
	/* This is global limit. Since we don't have a global structure,
	 * will be placing it in each channel.
	 */
	unsigned long p9_max_pages;
        /*
         * CHANGE:   Redefine magic # to Xen appropriate name
	 * Scatterlist: can be too big for stack. 
         */      
	struct scatterlist sg[NUM_P9_SGLISTS];

	int tag_len;
	/*
	 * tag name to identify a mount Non-null terminated
	 */
	char *tag;

	struct list_head chan_list;
};

static struct list_head xen9p_chan_list;

