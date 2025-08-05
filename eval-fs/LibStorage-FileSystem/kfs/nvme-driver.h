#ifndef __LIBDRIVER_KERNEL_H__
#define __LIBDRIVER_KERNEL_H__

#include <linux/cdev.h>
#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/nvme.h>
#include <linux/types.h>
#include <linux/uaccess.h>

extern int do_uintr_register_irq_handler(void (*handler)(void), int virq);

#define DEVICE_NAME "libnvmed"
#define CLASS_NAME "libnvmed_class"

#define PCI_CLASS_NVME 0x010802
#define LIBNVMED_MAGIC 'L'
#define LIBNVMED_OPEN_DEVICE _IOWR(LIBNVMED_MAGIC, 0, u64)
#define LIBNVMED_CLOSE_DEVICE _IOWR(LIBNVMED_MAGIC, 1, u64)

#define LIBNVMED_CREATE_QP _IOWR(LIBNVMED_MAGIC, 10, u64)
#define LIBNVMED_DELETE_QP _IOWR(LIBNVMED_MAGIC, 11, u64)

#define LIBNVMED_SET_PRIORITY _IOWR(LIBNVMED_MAGIC, 20, u64)

#define LIBNVMED_IRQ_POLL _IOWR(LIBNVMED_MAGIC, 30, u64)

// #define LIBNVMED_CREATE_DMABUFFER _IOWR(LIBNVMED_MAGIC, 20, u64)
// #define LIBNVMED_DELETE_DMABUFFER _IOWR(LIBNVMED_MAGIC, 21, u64)

#define MAX_QPAIRS 65536

enum {
    NVME_FEAT_ARB_HPW_SHIFT = 24,
    NVME_FEAT_ARB_MPW_SHIFT = 16,
    NVME_FEAT_ARB_LPW_SHIFT = 8,
    NVME_FEAT_ARB_AB_SHIFT = 0,
};

struct libnvmed_set_priority_args {
    u8 nid;
    u32 l_weight;
    u32 m_weight;
    u32 h_weight;
};

struct libnvmed_queue_args {
    // Create queue pair argument
    u8 nid;
    // Delete queue pair argument
    u8 qid;
    // Create queue pair argument
    // 0: polling
    // 1: interrupt
    // 2: user interrupt
    u8 interrupt;
    void (*uintr_handler)(void);
    u8 upid_idx;
    // Create queue pair argument
    // The capacity of the queue
    u16 depth;
    // Create queue pair argument
    int flags;
};

struct libnvmed_nvme_info {
    u8 nid;
    u16 num_user_io_queues;
    char path[32];
    u32 hardware_queue_count;
    int lba_shift;
    uint32_t nsid;
    DECLARE_BITMAP(qid_bitmap, MAX_QPAIRS);

    struct pci_dev *pdev;
    struct nvme_dev *dev;

    struct list_head user_qps;

    // IRQ support
    unsigned int vec_max; // 0 based
    unsigned int vec_kernel;
    unsigned long *vec_bmap;
    unsigned int vec_bmap_max;
    struct nvme_irq_desc *desc;
    struct msix_entry *msix_entry;
};
struct uintr_upid {
    struct {
        u8 status;    /* bit 0: ON, bit 1: SN, bit 2-7: reserved */
        u8 reserved1; /* Reserved */
        u8 nv;        /* Notification vector */
        u8 reserved2; /* Reserved */
        u32 ndst;     /* Notification destination */
    } nc __packed;    /* Notification control */
    u64 puir;         /* Posted user interrupt requests */
} __aligned(64);
struct libnvmed_qp_info {
    struct libnvmed_nvme_info *nvme_info;
    struct list_head next_qp;

    struct nvme_command *sqes;
    struct nvme_completion *cqes;

    dma_addr_t sq_dma_addr;
    dma_addr_t cq_dma_addr;

    u32 __iomem *q_db;
    u16 q_depth;
    u16 sq_head;
    u16 sq_tail;
    u16 cq_head;
    u16 qid;
    pid_t owner_pid;
    int flags;

    struct cdev cq_cdev;
    struct cdev sq_cdev;
    struct cdev db_cdev;
    struct cdev upid_cdev;
    // Intr Support
    // virtual irq vector.
    unsigned int irq_vector;
    char *irq_name;
    atomic_t nr_intr;

    // Uintr support
    // May need hardware irq vector
};

struct libnvmed_open_device_args {
    // Close device argument
    uint8_t nid;
    // Open device argument
    char path[32];
    // Open device result
    uint32_t db_stride;
    // Open device result
    uint32_t max_hw_sectors;

    // Open device result
    int lba_shift;
    // Open device result
    uint32_t nsid;
};

#define DMA_DEVICE(nvme_info) (&nvme_info->pdev->dev)

#define MAX_NVME_DEVICES 32

// IRQ support
struct nvme_irq_desc {
    int cq_vector;
    irq_handler_t handler;
    irq_handler_t thread_fn;
    const struct cpumask *affinity_hint;
    const char *irqName;
    void *queue;
};

// Kernel types
// 6.9.6

#include <linux/blk-mq.h>

struct nvme_fault_inject {
#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
    struct fault_attr attr;
    struct dentry *parent;
    bool dont_retry; /* DNR, do not retry */
    u16 status;      /* status code */
#endif
};

enum nvme_ctrl_state {
    NVME_CTRL_NEW,
    NVME_CTRL_LIVE,
    NVME_CTRL_RESETTING,
    NVME_CTRL_CONNECTING,
    NVME_CTRL_DELETING,
    NVME_CTRL_DELETING_NOIO,
    NVME_CTRL_DEAD,
};

struct nvme_ctrl {
    bool comp_seen;
    bool identified;
    bool passthru_err_log_enabled;
    enum nvme_ctrl_state state;
    spinlock_t lock;
    struct mutex scan_lock;
    const struct nvme_ctrl_ops *ops;
    struct request_queue *admin_q;
    struct request_queue *connect_q;
    struct request_queue *fabrics_q;
    struct device *dev;
    int instance;
    int numa_node;
    struct blk_mq_tag_set *tagset;
    struct blk_mq_tag_set *admin_tagset;
    struct list_head namespaces;
    struct rw_semaphore namespaces_rwsem;
    struct device ctrl_device;
    struct device *device; /* char device */
#ifdef CONFIG_NVME_HWMON
    struct device *hwmon_device;
#endif
    struct cdev cdev;
    struct work_struct reset_work;
    struct work_struct delete_work;
    wait_queue_head_t state_wq;

    struct nvme_subsystem *subsys;
    struct list_head subsys_entry;

    struct opal_dev *opal_dev;

    char name[12];
    u16 cntlid;

    u16 mtfa;
    u32 ctrl_config;
    u32 queue_count;

    u64 cap;
    u32 max_hw_sectors;
    u32 max_segments;
    u32 max_integrity_segments;
    u32 max_zeroes_sectors;
#ifdef CONFIG_BLK_DEV_ZONED
    u32 max_zone_append;
#endif
    u16 crdt[3];
    u16 oncs;
    u8 dmrl;
    u32 dmrsl;
    u16 oacs;
    u16 sqsize;
    u32 max_namespaces;
    atomic_t abort_limit;
    u8 vwc;
    u32 vs;
    u32 sgls;
    u16 kas;
    u8 npss;
    u8 apsta;
    u16 wctemp;
    u16 cctemp;
    u32 oaes;
    u32 aen_result;
    u32 ctratt;
    unsigned int shutdown_timeout;
    unsigned int kato;
    bool subsystem;
    unsigned long quirks;
    struct nvme_id_power_state psd[32];
    struct nvme_effects_log *effects;
    struct xarray cels;
    struct work_struct scan_work;
    struct work_struct async_event_work;
    struct delayed_work ka_work;
    struct delayed_work failfast_work;
    struct nvme_command ka_cmd;
    unsigned long ka_last_check_time;
    struct work_struct fw_act_work;
    unsigned long events;

#ifdef CONFIG_NVME_MULTIPATH
    /* asymmetric namespace access: */
    u8 anacap;
    u8 anatt;
    u32 anagrpmax;
    u32 nanagrpid;
    struct mutex ana_lock;
    struct nvme_ana_rsp_hdr *ana_log_buf;
    size_t ana_log_size;
    struct timer_list anatt_timer;
    struct work_struct ana_work;
#endif

#ifdef CONFIG_NVME_HOST_AUTH
    struct work_struct dhchap_auth_work;
    struct mutex dhchap_auth_mutex;
    struct nvme_dhchap_queue_context *dhchap_ctxs;
    struct nvme_dhchap_key *host_key;
    struct nvme_dhchap_key *ctrl_key;
    u16 transaction;
#endif
    struct key *tls_key;

    /* Power saving configuration */
    u64 ps_max_latency_us;
    bool apst_enabled;

    /* PCIe only: */
    u16 hmmaxd;
    u32 hmpre;
    u32 hmmin;
    u32 hmminds;

    /* Fabrics only */
    u32 ioccsz;
    u32 iorcsz;
    u16 icdoff;
    u16 maxcmd;
    int nr_reconnects;
    unsigned long flags;
    struct nvmf_ctrl_options *opts;

    struct page *discard_page;
    unsigned long discard_page_busy;

    struct nvme_fault_inject fault_inject;

    enum nvme_ctrl_type cntrltype;
    enum nvme_dctype dctype;
};

/*
 * List of workarounds for devices that required behavior not specified in
 * the standard.
 */
enum nvme_quirks {
    /*
     * Prefers I/O aligned to a stripe size specified in a vendor
     * specific Identify field.
     */
    NVME_QUIRK_STRIPE_SIZE = (1 << 0),

    /*
     * The controller doesn't handle Identify value others than 0 or 1
     * correctly.
     */
    NVME_QUIRK_IDENTIFY_CNS = (1 << 1),

    /*
     * The controller deterministically returns O's on reads to
     * logical blocks that deallocate was called on.
     */
    NVME_QUIRK_DEALLOCATE_ZEROES = (1 << 2),

    /*
     * The controller needs a delay before starts checking the device
     * readiness, which is done by reading the NVME_CSTS_RDY bit.
     */
    NVME_QUIRK_DELAY_BEFORE_CHK_RDY = (1 << 3),

    /*
     * APST should not be used.
     */
    NVME_QUIRK_NO_APST = (1 << 4),

    /*
     * The deepest sleep state should not be used.
     */
    NVME_QUIRK_NO_DEEPEST_PS = (1 << 5),

    /*
     * Set MEDIUM priority on SQ creation
     */
    NVME_QUIRK_MEDIUM_PRIO_SQ = (1 << 7),

    /*
     * Ignore device provided subnqn.
     */
    NVME_QUIRK_IGNORE_DEV_SUBNQN = (1 << 8),

    /*
     * Broken Write Zeroes.
     */
    NVME_QUIRK_DISABLE_WRITE_ZEROES = (1 << 9),

    /*
     * Force simple suspend/resume path.
     */
    NVME_QUIRK_SIMPLE_SUSPEND = (1 << 10),

    /*
     * Use only one interrupt vector for all queues
     */
    NVME_QUIRK_SINGLE_VECTOR = (1 << 11),

    /*
     * Use non-standard 128 bytes SQEs.
     */
    NVME_QUIRK_128_BYTES_SQES = (1 << 12),

    /*
     * Prevent tag overlap between queues
     */
    NVME_QUIRK_SHARED_TAGS = (1 << 13),

    /*
     * Don't change the value of the temperature threshold feature
     */
    NVME_QUIRK_NO_TEMP_THRESH_CHANGE = (1 << 14),

    /*
     * The controller doesn't handle the Identify Namespace
     * Identification Descriptor list subcommand despite claiming
     * NVMe 1.3 compliance.
     */
    NVME_QUIRK_NO_NS_DESC_LIST = (1 << 15),

    /*
     * The controller does not properly handle DMA addresses over
     * 48 bits.
     */
    NVME_QUIRK_DMA_ADDRESS_BITS_48 = (1 << 16),

    /*
     * The controller requires the command_id value be limited, so skip
     * encoding the generation sequence number.
     */
    NVME_QUIRK_SKIP_CID_GEN = (1 << 17),

    /*
     * Reports garbage in the namespace identifiers (eui64, nguid, uuid).
     */
    NVME_QUIRK_BOGUS_NID = (1 << 18),

    /*
     * No temperature thresholds for channels other than 0 (Composite).
     */
    NVME_QUIRK_NO_SECONDARY_TEMP_THRESH = (1 << 19),

    /*
     * Disables simple suspend/resume path.
     */
    NVME_QUIRK_FORCE_NO_SIMPLE_SUSPEND = (1 << 20),

    /*
     * MSI (but not MSI-X) interrupts are broken and never fire.
     */
    NVME_QUIRK_BROKEN_MSI = (1 << 21),
};

struct nvme_dev {
    struct nvme_queue *queues;
    struct blk_mq_tag_set tagset;
    struct blk_mq_tag_set admin_tagset;
    u32 __iomem *dbs;
    struct device *dev;
    struct dma_pool *prp_page_pool;
    struct dma_pool *prp_small_pool;
    unsigned online_queues;
    unsigned max_qid;
    unsigned io_queues[HCTX_MAX_TYPES];
    unsigned int num_vecs;
    u32 q_depth;
    int io_sqes;
    u32 db_stride;
    void __iomem *bar;
    unsigned long bar_mapped_size;
    struct mutex shutdown_lock;
    bool subsystem;
    u64 cmb_size;
    bool cmb_use_sqes;
    u32 cmbsz;
    u32 cmbloc;
    struct nvme_ctrl ctrl;
    u32 last_ps;
    bool hmb;

    mempool_t *iod_mempool;

    /* shadow doorbell buffer support: */
    __le32 *dbbuf_dbs;
    dma_addr_t dbbuf_dbs_dma_addr;
    __le32 *dbbuf_eis;
    dma_addr_t dbbuf_eis_dma_addr;

    /* host memory buffer support: */
    u64 host_mem_size;
    u32 nr_host_mem_descs;
    dma_addr_t host_mem_descs_dma;
    struct nvme_host_mem_buf_desc *host_mem_descs;
    void **host_mem_desc_bufs;
    unsigned int nr_allocated_queues;
    unsigned int nr_write_queues;
    unsigned int nr_poll_queues;
};

enum nvme_ns_features {
    NVME_NS_EXT_LBAS = 1 << 0,           /* support extended LBA format */
    NVME_NS_METADATA_SUPPORTED = 1 << 1, /* support getting generated md */
    NVME_NS_DEAC, /* DEAC bit in Write Zeores supported */
};

struct nvme_ns_ids {
    u8 eui64[8];
    u8 nguid[16];
    uuid_t uuid;
    u8 csi;
};

struct nvme_ns_head {
    struct list_head list;
    struct srcu_struct srcu;
    struct nvme_subsystem *subsys;
    struct nvme_ns_ids ids;
    struct list_head entry;
    struct kref ref;
    bool shared;
    bool passthru_err_log_enabled;
    int instance;
    struct nvme_effects_log *effects;
    u64 nuse;
    unsigned ns_id;
    int lba_shift;
    u16 ms;
    u16 pi_size;
    u8 pi_type;
    u8 pi_offset;
    u8 guard_type;
    u16 sgs;
    u32 sws;
#ifdef CONFIG_BLK_DEV_ZONED
    u64 zsze;
#endif
    unsigned long features;

    struct ratelimit_state rs_nuse;

    struct cdev cdev;
    struct device cdev_device;

    struct gendisk *disk;
#ifdef CONFIG_NVME_MULTIPATH
    struct bio_list requeue_list;
    spinlock_t requeue_lock;
    struct work_struct requeue_work;
    struct mutex lock;
    unsigned long flags;
#define NVME_NSHEAD_DISK_LIVE 0
    struct nvme_ns __rcu *current_path[];
#endif
};

struct nvme_ns {
    struct list_head list;

    struct nvme_ctrl *ctrl;
    struct request_queue *queue;
    struct gendisk *disk;
#ifdef CONFIG_NVME_MULTIPATH
    enum nvme_ana_state ana_state;
    u32 ana_grpid;
#endif
    struct list_head siblings;
    struct kref kref;
    struct nvme_ns_head *head;

    unsigned long flags;
#define NVME_NS_REMOVING 0
#define NVME_NS_ANA_PENDING 2
#define NVME_NS_FORCE_RO 3
#define NVME_NS_READY 4

    struct cdev cdev;
    struct device cdev_device;

    struct nvme_fault_inject fault_inject;
};

#endif