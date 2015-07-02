#define P9_RSP_ERROR       -1
#define P9_RSP_OKAY         0

struct p9_request {
        uint64_t        id;          /* I'm me */
        uint8_t        operation;    /* BLKIF_OP_???            */
} __attribute__((__packed__));

struct p9_response {
        uint64_t        id;              /* copied from request */
        uint8_t         operation;       /* copied from request */
        int16_t         status;          /* BLKIF_RSP_???       */
};

enum p9_state {
    P9_STATE_DISCONNECTED,
    P9_STATE_CONNECTED,
    P9_STATE_SUSPENDED,
};

#define P9_RING_SIZE __CONST_RING_SIZE(p9, PAGE_SIZE);

DEFINE_RING_TYPES(p9, struct p9_request, struct p9_response);

union p9_back_rings {
	struct blkif_back_ring        native;
	struct blkif_common_back_ring common;
};

struct xen_p9if {
	/* Unique identifier for this interface. */
	domid_t			domid;
	unsigned int		handle;
	/* Physical parameters of the comms window. */
	unsigned int		irq;
	/* Comms information. */
	union p9_back_rings	p9_rings;
	void			*p9_ring;
	/* Back pointer to the backend_info. */
	struct backend_info	*be;
	/* Private fields. */
	spinlock_t		p9_ring_lock;
	atomic_t		refcnt;

	wait_queue_head_t	wq;

	unsigned int		waiting_reqs;

	/* buffer of free pages to map grant refs */
	spinlock_t		free_pages_lock;
	int			free_pages_num;
	struct list_head	free_pages;

	/* List of all 'pending_req' available */
	struct list_head	pending_free;
	/* And its spinlock. */
	spinlock_t		pending_free_lock;
	wait_queue_head_t	pending_free_wq;

	/* statistics */
	unsigned long		                st_print;
	unsigned long long			st_rd_req;
	unsigned long long			st_wr_req;
	unsigned long long			st_oo_req;
	unsigned long long			st_f_req;
	unsigned long long			st_ds_req;
	unsigned long long			st_rd_sect;
	unsigned long long			st_wr_sect;

	struct work_struct	free_work;
	/* Thread shutdown wait queue. */
	wait_queue_head_t	shutdown_wq;
};

