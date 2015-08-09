/*
 * The Xen 9p transport driver
 *
 *  This is a prototype for a Xen 9p transport driver.  
 *  This header file contains definitions common to the front end of
 *  this transport.
 *  There are 3 .c files for the front end, that share this header file:
 *       p9_front.c  manages the xen communication with the backend
 *       p9_front_driver.c provides the xenbus driver function interface
 *       trans_xen9p.c that provides the 9p client interface
 *  
 *  Copyright (C) 2015 Linda Jacobson
 *  
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

struct p9_front_info;

/*
 * struct xen9p_chan - per-instance transport information
 * @inuse: whether the channel is in use
 * @lock: protects multiple elements within this structure
 * @client: client instance
 * @drv_info  : device specific information including xendev associated with
 *          this channel;  NOTE:  info (below) contains pointer to chan
 *          This is not optimal, but allows me to make as few changes as 
 *          possible to template code.
 * @sg: scatter gather list which is used to pack a request (protected?)
 *
 * We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure needs to be put in the transport private.
 *
 */

struct xen9p_chan {
	bool			inuse;

	spinlock_t		lock;

	struct p9_client	*client;
	struct p9_front_info	*drv_info;
	//  CHANGE? probably work_struct in info is better place
	wait_queue_head_t 	*vc_wq;  

	/* This is global limit. Since we don't have a global structure,
	 * will be placing it in each channel.
	 */
	unsigned long		p9_max_pages;
	/*
	 * CHANGE:   Redefine magic # to Xen appropriate name
	 * Scatterlist: can be too big for stack. 
	 */
	struct scatterlist	sg[NUM_P9_SGLISTS];
  
	int			tag_len;
	char			*tag;   /* tag to identify mount name: diff from client tag*/

	struct list_head	chan_list;
};

struct grant {
	grant_ref_t gref;
	unsigned long pfn;
	struct list_head node;
};

/*
 * struct 9pfront_info - per-instance "device" information
 *                  device specific information including xendev associated with
 *                    this channel
 * @io_lock : protects multiple elements within this structure
 * @mutex   : ditto
 * @xbdev   : xenbus device info
 * @chan    : per instance transport info
 *   NOTE:  chan contains pointer to info 
 *          This is not optimal, but allows me to make as few changes as 
 *          possible to template code.
 * @p9state  : connected, disconnected or suspended
 * @ring_ref : gref for the ring
 * @page     : current page being worked on - temp while only one
 * @offset   : offset in page for next request's data - ditto on temp
 * @addresses: addresses where data is xferred from/to per request
 *
 *
 */

struct p9_front_info {
	spinlock_t 		io_lock;
	struct mutex	 	mutex;
	struct xenbus_device 	*xbdev;
	enum p9_state 		connected;
	int			ring_ref;
	struct p9_front_ring 	ring;
	unsigned int 		evtchn;
	unsigned int		irq;
	struct list_head	grants;
	struct page		*page;
	unsigned int		offset;
	void 			*addresses[PAGE_SIZE];	
	struct xen9p_chan 	*chan;
	int			is_ready;
};

/* 
 * for communication between xenbus driver code and 9p client interface
 * at start up and cleanup
 */
void init_xen_9p(void);
void cleanup_xen_9p(void);
/* 
 * Common code used when first setting up, and when resuming. 
 *
 * Defined in p9_front.c
 * Accessed in p9_front_driver.c
 */
int talk_to_9p_back(struct xenbus_device *dev, struct p9_front_info *info);
void p9_free(struct p9_front_info *info, int suspend);
void p9front_connect(struct p9_front_info *info);
void p9front_closing(struct p9_front_info *info);
void p9_handle_response(struct p9_response *bret,
			struct p9_front_info *info);
int p9front_handle_client_request (struct p9_front_info *info,
				    void *req_metadata, int metadata_len,
				    char *out_data, int out_len,
				    char *in_data, int in_len);
void req_done(void *metadata, struct xen9p_chan *chan, int16_t status);
void p9_xen_close(struct p9_client *client);
