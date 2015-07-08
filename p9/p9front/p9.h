#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>


#define P9_RSP_ERROR       -1
#define P9_RSP_OKAY         0

struct p9_request {
        uint64_t        id;          /* I'm me */
        uint8_t        operation;    /* BLKIF_OP_???            */
} __attribute__((__packed__));

typedef struct p9_request p9_request_t;

struct p9_response {
        uint64_t        id;              /* copied from request */
        uint8_t         operation;       /* copied from request */
        int16_t         status;          /* BLKIF_RSP_???       */
};

typedef struct p9_response p9_response_t;

enum p9_state {
    P9_STATE_DISCONNECTED,
    P9_STATE_CONNECTED,
    P9_STATE_SUSPENDED,
};

#define P9_RING_SIZE __CONST_RING_SIZE(p9, PAGE_SIZE)

DEFINE_RING_TYPES(p9, struct p9_request, struct p9_response);

struct xen_p9if {
	/* Unique identifier for this interface. */
	domid_t			domid;
	unsigned int		handle;
	/* Physical parameters of the comms window. */
	unsigned int		irq;
	/* Comms information. */
        struct p9_back_ring	p9_bring;
	void			*p9_ring;
	/* Back pointer to the backend_info. */
	struct backend_info	*be;

};


