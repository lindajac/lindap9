#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>

#include "hw/hw.h"
#include "hw/xen/xen_backend.h"
#include "p9.h"


/* ------------------------------------------------------------- */


static int max_requests = 2;

/* ------------------------------------------------------------- */

#define BLOCK_SIZE  512
#define P9_MAX_SEGMENTS_PER_REQUEST 1



struct XenP9Dev {
    struct XenDevice    xendev;  /* must be first */
    char                *params;
    char                *mode;
    char                *type;

    int                 ring_ref;
    void                *sring;
    int                 *page;
  
    p9_back_ring_t      ring;
   
    int                 cnt_map;
   
    unsigned int        max_grants;

    /* qemu block driver */
    DriveInfo           *dinfo;
    BlockDriverState    *bs;
    QEMUBH              *bh;
};

/* ------------------------------------------------------------- */

static void p9_bh(void *opaque)
{
  //    struct XenP9Dev *p9dev = opaque;
    //    p9_handle_requests(p9dev);
}

/*
 * We need to account for the grant allocations requiring contiguous
 * chunks; the worst case number would be
 *     max_req * max_seg + (max_req - 1) * (max_seg - 1) + 1,
 * but in order to keep things simple just use
 *     2 * max_req * max_seg.
 */
#define MAX_GRANTS(max_req, max_seg) (2 * (max_req) * (max_seg))


static void p9_alloc(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);
   
    p9dev->bh = qemu_bh_new(p9_bh, p9dev);
  
    if (xc_gnttab_set_max_grants(xendev->gnttabdev,
            MAX_GRANTS(max_requests, 1)) < 0) {
        xen_be_printf(xendev, 0, "xc_gnttab_set_max_grants failed: %s\n",
                      strerror(errno));
    }
}

static int p9_init(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);
   
    /* read xenstore entries */
    if (p9dev->params == NULL) {
       goto out_error;
   }
   return 0;

out_error:
    return -1;
}

static void p9_connect(struct XenDevice *xendev)
{
  //    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);
    grant_ref_t gref;
    int *page;
    
    // for now
   void *pgref = &gref;
   if (xenstore_read_fe_int(xendev, "gref",
			       (int *)pgref) == -1) {
        return;
    }
   
    page  = xc_gnttab_map_grant_ref(xendev->gnttabdev,
					   xendev->dom,
                                           gref,
                                           PROT_READ | PROT_WRITE);
    if (!page) {
        return;
    }
    printf ("I read %d\n", page[0]);
}

/*
 * initializing to sync up w/ front-end
 */
static int p9_initwfe(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);
 
    if (xenstore_read_fe_int(&p9dev->xendev, "ring-ref", &p9dev->ring_ref) == -1)   {
        return -1;
    }
    if (xenstore_read_fe_int(&p9dev->xendev, "event-channel",
                             &p9dev->xendev.remote_port) == -1) {
        return -1;
    }
   
    p9dev->sring = xc_gnttab_map_grant_ref(p9dev->xendev.gnttabdev,
					    p9dev->xendev.dom,
                                            p9dev->ring_ref,
                                            PROT_READ | PROT_WRITE);
    if (!p9dev->sring) {
        return -1;
    }

    p9_sring_t *sring  = p9dev->sring;
    BACK_RING_INIT(&p9dev->ring, sring, XC_PAGE_SIZE);
 
    xen_be_bind_evtchn(&p9dev->xendev);

    xen_be_printf(&p9dev->xendev, 1, "ok: ring-ref %d, "
                   "remote port %d, local port %d\n",
                   p9dev->ring_ref,  p9dev->xendev.remote_port, p9dev->xendev.local_port);
    return 0;
}

static void p9_disconnect(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);

    if (p9dev->bs) {
        p9dev->bs = NULL;
    }
    xen_be_unbind_evtchn(&p9dev->xendev);

    if (p9dev->sring) {
       p9dev->sring = NULL;
    }
}

static int p9_free(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);
  
    if (p9dev->sring) {
        p9_disconnect(xendev);
    }

    g_free(p9dev->params);
    qemu_bh_delete(p9dev->bh);
    return 0;
}

static void p9_event(struct XenDevice *xendev)
{
    struct XenP9Dev *p9dev = container_of(xendev, struct XenP9Dev, xendev);

    qemu_bh_schedule(p9dev->bh);
}

struct XenDevOps xen_p9_ops = {
    .size       = sizeof(struct XenP9Dev),
    .flags      = DEVOPS_FLAG_NEED_GNTDEV,
    .alloc      = p9_alloc,   // just been created
    .init       = p9_init,    // prior to synching w/ front-end
    .initialise = p9_initwfe, //initialization when front-end ready
    .connected  = p9_connect, // fe is connected
    .disconnect = p9_disconnect,
    .event      = p9_event,
    .free       = p9_free,  // it's being deleted
};

   
