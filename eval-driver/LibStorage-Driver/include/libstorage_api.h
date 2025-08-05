#ifndef LIBSTORAGE_API_H
#define LIBSTORAGE_API_H

// Common for driver & user application

#include "libstorage_common.h"
#include "libstorage_ioctl.h"
#include "linux/module.h"
#include <stdatomic.h>
#include <sys/queue.h>

#define DMA_PER_DEVICE 128
#define MAX_IO_REQS    65536

struct dma_pool;
struct prp_page_list;
struct io_desc;

enum NVMe {
	NVME_SQ_PRIO_URGENT = (0 << 1),
	NVME_SQ_PRIO_HIGH = (1 << 1),
	NVME_SQ_PRIO_MEDIUM = (2 << 1),
	NVME_SQ_PRIO_LOW = (3 << 1),
};
typedef struct dma_buffer {
	void *buf;
	uint64_t *pa;
	uint32_t size;
} dma_buffer;

typedef struct io_request {
	TAILQ_HEAD(, io_request) children;
	TAILQ_ENTRY(io_request) child_tailq;

	STAILQ_ENTRY(io_request) stailq;
	struct io_request *parent;

	uint16_t num_children;
	uint16_t submit_children;

	void (*callback)(void *data);
	void *data;

	uint64_t lba;
	uint32_t lba_count;
	int opcode;

	struct ls_nvme_qp *qp;
	struct nvme_command *cmd;

	uint64_t prp1;
	uint64_t prp2;
	void *prp2_addr;

	int flag;

} io_request;
typedef struct ls_nvme_dev {
	uint32_t id;
	char *path;
	uint32_t db_stride;
	uint32_t max_hw_sectors;

	struct dma_pool *dma_pool;
	struct prp_page_list *prp_list;

	uint32_t nsid;
	uint32_t lba_shift;
	uint64_t st_sector;

	int upid_fd;
	uintr_upid *upid;
	uintr_upid *upid_page;
#ifdef MPK_PROTECTION
	int pkey;
	uint32_t in_trust;
	uint32_t out_trust;
#endif

} ls_nvme_dev;

typedef struct ls_nvme_qp {
	ls_nvme_dev *nvme;
	uint16_t qid;
	uint16_t depth;
	uint16_t sq_head, sq_tail, cq_head;
	uint8_t cq_phase;

	int sq_fd;
	int cq_fd;
	int db_fd;

	struct nvme_command *sqes;
	volatile struct nvme_completion *cqes;

	void *dbs;
	uint32_t *sq_db;
	uint32_t *cq_db;

	struct io_desc *iods;
	uint32_t iod_pos;
	uint8_t interrupt;

	uint32_t num_req;
	uint32_t req_pos;
	io_request *req_buf;

	struct user_task_ctx *task_ctx;
} ls_nvme_qp;

int open_device(ls_device_args *args, ls_nvme_dev *nvme);
int close_device(ls_device_args *args, ls_nvme_dev *nvme);
int register_uintr(ls_device_args *args, ls_nvme_dev *nvme);
int create_qp(ls_queue_args *args, ls_nvme_dev *nvme, ls_nvme_qp *qp);
int delete_qp(ls_queue_args *args, ls_nvme_dev *nvme, ls_nvme_qp *qp);
int ls_sched_yield(ls_device_args *args);
int create_dma_buffer(ls_nvme_dev *nvme, dma_buffer *buf, uint32_t size);
int delete_dma_buffer(ls_nvme_dev *nvme, dma_buffer *buf);

int read_blk(ls_nvme_qp *qp, uint64_t lba, uint32_t count, dma_buffer *buf);
int write_blk(ls_nvme_qp *qp, uint64_t lba, uint32_t count, dma_buffer *buf);

int read_blk_async(ls_nvme_qp *qp, uint64_t lba, uint32_t count,
		   dma_buffer *buf, void (*callback)(void *), void *data);
int write_blk_async(ls_nvme_qp *qp, uint64_t lba, uint32_t count,
		    dma_buffer *buf, void (*callback)(void *), void *data);
int ls_qpair_process_completions(ls_nvme_qp *qp, int maxn);

#endif // LIBSTORAGE_API_H