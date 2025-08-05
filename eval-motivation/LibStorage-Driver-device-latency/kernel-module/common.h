#ifndef COMMON_H
#define COMMON_H

#include "libstorage_common.h"

#include "linux/sched.h"
#include "linux/semaphore.h"
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/nvme.h>
#include <linux/blk-mq.h>
#include <linux/dma-mapping.h>
#include <linux/cdev.h>

#define MAX_NVME_DEVICES  32
#define MAX_NVME_INSTANCE 64
#define MAX_NVME_QPAIRS	  256

extern int ls_cdev_major;
extern struct cdev ls_cdev;
extern struct class *ls_class;
extern struct device *ls_device;

#define CLASS_NAME "ls_class"
// From User Interrupt
extern struct uintr_upid *upid_page;
extern struct uintr_upid *init_upid_mem(void);
extern void free_upid_mem(void);
extern int do_uintr_register_irq_handler(void (*handler)(void), int virq);
extern u64 check_uirr(void);
extern void del_uinv(void);
// From NVMe
enum {
	NVME_FEAT_ARB_HPW_SHIFT = 24,
	NVME_FEAT_ARB_MPW_SHIFT = 16,
	NVME_FEAT_ARB_LPW_SHIFT = 8,
	NVME_FEAT_ARB_AB_SHIFT = 0,
};
extern int nvme_submit_sync_cmd(struct request_queue *q,
				struct nvme_command *cmd, void *buf,
				unsigned bufflen);
extern int nvme_set_queue_count(struct nvme_ctrl *ctrl, int *count);

extern int nvme_set_features(struct nvme_ctrl *dev, unsigned int fid,
			     unsigned int dword11, void *buffer, size_t buflen,
			     u32 *result);

extern int nvme_get_features(struct nvme_ctrl *dev, unsigned int fid,
			     unsigned int dword11, void *buffer, size_t buflen,
			     u32 *result);

typedef struct ls_nvme_dev {
	// attributes
	uint8_t id;
	uint32_t num_user_qpairs;
	uint32_t num_hardware_qp_limit;
	char path[PATH_SIZE];

	// namespace variables
	uint32_t nsid;
	uint32_t lba_shift;

	// kernel structures
	struct nvme_dev *dev;
	struct pci_dev *pdev;

	// queue structures
	struct list_head user_qpairs;
	DECLARE_BITMAP(qid_bitmap, MAX_NVME_QPAIRS);

	struct list_head user_dev_instance;

	// irq variables
	uint32_t irq_vec_max;
	DECLARE_BITMAP(vec_bmap, MAX_NVME_QPAIRS);
	uint32_t vec_bmap_size;

	struct msix_entry *msix_entry;
} ls_nvme_dev;

struct uintr_upid_ctx {
	struct list_head node;
	struct task_struct *task; /* Receiver task */
	u64 uvec_mask; /* track registered vectors per bit */
	struct uintr_upid *upid;
	/* TODO: Change to kernel kref api */
	refcount_t refs;
};
// Device Descriptors
typedef struct ls_nvme_dev_instance {
	// FIXME: Should Be Atomic
	int status;
	int instance_id;
	// TODO: to support more qpairs
	int qid;
	int upid_idx;
	void *upid;
	int virq;
	int irq_vector;
	char *irq_name;
	void (*uintr_handler)(void);
	struct cdev upid_cdev;
	pid_t owner_pid;
	struct task_struct *task;
	int num_uintr_kernel;

	ls_nvme_dev *nvme;
	struct list_head next_dev_instance;

	struct semaphore *sem;
	volatile int in_waiting;
} ls_nvme_dev_instance;

extern ls_nvme_dev nvme_devices[MAX_NVME_DEVICES];
extern ls_nvme_dev_instance nvme_instances[MAX_NVME_INSTANCE];
typedef struct ls_nvme_qp {
	ls_nvme_dev *nvme_dev;
	ls_nvme_dev_instance *instance;
	struct list_head next_qp;

	struct nvme_command *sqes;
	struct nvme_completion *cqes;

	dma_addr_t sqes_dma;
	dma_addr_t cqes_dma;

	uint32_t __iomem *q_db;
	uint16_t q_depth;
	uint16_t sq_head;
	uint16_t sq_tail;
	uint16_t cq_head;
	uint16_t qid;
	pid_t owner_pid;
	int flags;

	struct cdev cq_cdev;
	struct cdev sq_cdev;
	struct cdev db_cdev;
	// Intr Support
	// virtual irq vector.
	int interrupt;
} ls_nvme_qp;

static inline ls_nvme_dev *nid_to_nvme(const int nid)
{
	int i;
	for (i = 0; i < MAX_NVME_DEVICES; i++) {
		if (nvme_devices[i].id == nid) {
			return &nvme_devices[i];
		}
	}
	return NULL;
}
//Deprecated
ls_nvme_dev_instance *upid_idx_to_instance(ls_nvme_dev *nvme, int upid_idx);
// int ls_register_irq(ls_nvme_dev *nvme, ls_nvme_qp *qp, void *uihandler);
int ls_nvme_set_irq(ls_nvme_dev *nvme, ls_nvme_dev_instance *instance,
		    ls_device_args *args);
int ls_nvme_register_uintr(ls_nvme_dev *nvme, ls_nvme_dev_instance *instance,
			   ls_device_args *args);
void ls_destroy_nvme_irq(ls_nvme_dev_instance *instance);
int ls_do_qp_release(ls_nvme_dev *nvme, ls_nvme_qp *qp);
int ls_create_qp(void __user *__args);
int ls_delete_qp(void __user *__args);
int ls_set_priority(void __user *__args);
void kern_ls_sched_out(void __user *__args);

int ls_map_qp(ls_nvme_dev *nvme, int qid, ls_nvme_qp *qp);
int ls_map_nvme(ls_nvme_dev_instance *instance, int upid_idx);
void ls_unmap_nvme(ls_nvme_dev_instance *instance);
void ls_delete_qp_mapping(ls_nvme_qp *qp);
void ls_destroy_mmap_files(int qid);
void ls_delete_nvme_mapping(ls_nvme_dev *nvme);
int ls_reg_uintr(void __user *__args);

ls_nvme_qp *find_qp_by_qid(struct ls_nvme_dev *nvme, int qid);
#endif // COMMON_H