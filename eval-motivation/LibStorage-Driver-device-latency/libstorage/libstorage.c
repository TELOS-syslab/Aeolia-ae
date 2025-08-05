#include "dma_pool.h"
#include "libstorage_api.h"
#include "libstorage_common.h"
#include "libstorage_ioctl.h"
#include "libstorage.h"
#include "logger.h"
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/user.h>
ls_nvme_dev *g_nvme_dev;
// qp_node_t *g_qp_list;
__thread ls_nvme_qp *local_qp;
// ls_nvme_qp *local_qp;
static int ls_dev_fd = 0;
// ******************************* HELPER FUNCTIONS *******************************
static int find_first_zero_bit(uint64_t *addr, uint64_t size)
{
	uint64_t len = (size + sizeof(uint64_t) - 1) / sizeof(uint64_t);
	uint64_t i;
	for (i = 0; i < len; i++) {
		if (addr[i] != ~(uint64_t)0) {
			return i * sizeof(uint64_t) * 8 +
			       __builtin_ffsll(~addr[i]) - 1;
		}
	}
	return -1;
}
static void create_uintr_info_from_args(ls_device_args *args, ls_nvme_dev *nvme)
{
	char path[PATH_SIZE];
	LOG_INFO("Try to open UPID " LIBSTORAGE_UPID_PATH_FORMAT, nvme->id,
		 args->instance_id);
	snprintf(path, PATH_SIZE, "/dev/" LIBSTORAGE_UPID_PATH_FORMAT, nvme->id,
		 args->instance_id);

	nvme->upid_fd = open(path, O_RDWR);
	if (nvme->upid_fd < 0) {
		LOG_WARN("Failed to open UPID %s\n", path);
		return;
	}
	uintr_upid *upid_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
				     MAP_SHARED, nvme->upid_fd, 0);
	if (upid_page == MAP_FAILED) {
		LOG_WARN("Failed to mmap UPID %s\n", path);
		return;
	}
	nvme->upid_page = upid_page;
}
static void create_nvme_info_from_args(ls_device_args *args, ls_nvme_dev *nvme)
{
	nvme->id = args->nid;
	nvme->path = args->path;
	nvme->db_stride = args->db_stride;
	nvme->max_hw_sectors = args->max_hw_sectors;
	// nvme->max_hw_sectors = 32;
	nvme->nsid = args->nsid;
	nvme->lba_shift = args->lba_shift;

	// LOG_DEBUG("MMaped UPID, BaseAddr : %p, ActualAddr : %p, PIR : %lu",
	// 	  upid_page, nvme->upid, nvme->upid->puir);
}
static void create_qp_info_from_args(ls_queue_args *args, ls_nvme_qp *qp,
				     ls_nvme_dev *nvme)
{
	qp->nvme = nvme;
	qp->qid = args->qid;
	qp->depth = args->depth;
	qp->interrupt = args->interrupt;
}

// ******************************* PRP&DMA FUNCTIONS *******************************
static void alloc_new_prp_list(ls_nvme_dev *nvme)
{
	int ret = 0;
	prp_page_list *prp_list = calloc(1, sizeof(prp_page_list));
	memset(prp_list->bitmap, 0, sizeof(prp_list->bitmap));
	ret = alloc_dma_buffer(nvme->dma_pool, HUGE_PAGE_SIZE, &prp_list->va,
			       &prp_list->pa);
	if (ret < 0) {
		LOG_WARN("Failed to alloc dma buffer\n");
		return;
	}

	prp_list->next = nvme->prp_list;
	nvme->prp_list = prp_list;
	return;
}
static int create_dma_for_nvme(ls_nvme_dev *nvme, int instance_id)
{
	int ret = 0;
	nvme->prp_list = NULL;
	nvme->dma_pool = (dma_pool *)malloc(sizeof(dma_pool));
	ret = create_dma_pool(nvme->dma_pool, DMA_PER_DEVICE * HUGE_PAGE_SIZE,
			      instance_id);
	if (ret < 0) {
		LOG_WARN("Failed to create dma pool");
		return ret;
	}
	alloc_new_prp_list(nvme);

	return ret;
}
void *get_prp_buf(ls_nvme_dev *nvme, uint64_t *pa)
{
	prp_page_list *prp_list = nvme->prp_list;
	prp_page_list *prev = NULL;
	int pos;
	int ret;
	while (prp_list != NULL) {
		pos = find_first_zero_bit(prp_list->bitmap, 512);
		if (pos >= 0) {
			prp_list->bitmap[pos / 64] |= 1 << (pos % 64);
			*pa = prp_list->pa[pos];
			return (void *)(prp_list->va + pos * PAGE_SIZE);
		}
		prp_list = prp_list->next;
	}
	alloc_new_prp_list(nvme);
	return get_prp_buf(nvme, pa);
}
void *free_prp_buf(ls_nvme_dev *nvme, void *buf)
{
	prp_page_list *prp_list = nvme->prp_list;
	while (prp_list) {
		if (buf >= (void *)prp_list->va &&
		    buf < (void *)(prp_list->va + HUGE_PAGE_SIZE)) {
			prp_list->bitmap[((uint64_t)buf - prp_list->va) /
					 PAGE_SIZE / 64] &=
				~((uint64_t)1
				  << (((uint64_t)buf - prp_list->va) /
				      PAGE_SIZE % 64));
			return buf;
		}
		prp_list = prp_list->next;
	}
	return NULL;
}
int make_prp_list(ls_nvme_dev *nvme, void *buf, uint32_t iosize,
		  uint64_t *paBase, uint64_t *prp1, uint64_t *prp2,
		  void **prp2_addr)
{
	int ret = 0;
	int i;
	int numBuf = (iosize + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t *prpBuf;
	uint64_t __prp1, __prp2 = 0;
	_clui();
	if ((uint64_t)buf % PAGE_SIZE != 0) {
		LOG_ERROR("Buffer is not page aligned");
		return -EINVAL;
	}
	__prp1 = paBase[0];
	if (numBuf == 1) {
		__prp2 = 0;
	} else if (numBuf == 2) {
		__prp2 = paBase[1];
	} else {
		prpBuf = get_prp_buf(nvme, &__prp2);
		*prp2_addr = prpBuf;
		for (i = 1; i < numBuf; i++) {
			prpBuf[i - 1] = paBase[i];
		}
	}

	*prp1 = __prp1;
	*prp2 = __prp2;
	_stui();
	return ret;
}
// ******************************* INTERFACE FUNCTIONS *******************************
int open_device(ls_device_args *args, ls_nvme_dev *nvme)
{
	int ret = 0;
	LOG_DEBUG("Open Device");
	// if (g_nvme_dev) {
	// 	// TODO: This is a workaround due to the uintr implementation
	// 	LOG_ERROR("Only Support Open One Device For Now");
	// 	return -EINVAL;
	// }
	if (!ls_dev_fd) {
		ls_dev_fd = open("/dev/" DEVICE_NAME, O_RDWR);
		if (ls_dev_fd < 0) {
			LOG_ERROR("Failed to open libstorage device\n");
			return ls_dev_fd;
		}
	}
	args->uintr_handler = (void *)ls_nvme_irq_user_handler;
	ret = ioctl_open_device(args);
	if (ret < 0) {
		LOG_WARN("Failed to open device\n");
		return ret;
	}
	LOG_INFO("Open kern device success");
	create_nvme_info_from_args(args, nvme);
	create_uintr_info_from_args(args, nvme);
	create_dma_for_nvme(nvme, args->instance_id);
	LOG_INFO("Create DMA for NVMe instance %d success", args->instance_id);
	return ret;
}
int close_device(ls_device_args *args, ls_nvme_dev *nvme)
{
	LOG_WARN("unmap upid : %p", (void *)(nvme->upid - args->upid_idx));
	munmap((void *)(nvme->upid - args->upid_idx), PAGE_SIZE);
	close(nvme->upid_fd);
	delete_dma_pool(nvme->dma_pool);
	free(nvme->dma_pool);
	int ret = 0;
	ret = ioctl_close_device(args);
	if (ret < 0) {
		LOG_WARN("Failed to close device\n");
		return ret;
	}
	memset(nvme, 0, sizeof(ls_nvme_dev));
	_clui();
	return ret;
}
int register_uintr(ls_device_args *args, ls_nvme_dev *nvme)
{
	int ret = ioctl_register_uintr(args);
	if (ret) {
		LOG_ERROR("failed to register uintr");
		return -1;
	}
	nvme->upid = (uintr_upid *)nvme->upid_page + args->upid_idx;
	_stui();
	return 0;
}

int create_qp(ls_queue_args *args, ls_nvme_dev *nvme, ls_nvme_qp *qp)
{
	int ret = 0;
	char path[PATH_SIZE];
	uint32_t *q_dbs;
	size_t req_size_padded;

	ret = ioctl_create_qp(args);
	if (ret < 0) {
		LOG_WARN("Failed to create qp\n");
		return ret;
	}

	create_qp_info_from_args(args, qp, nvme);

	LOG_DEBUG("Created QP Info QID : %d, Depth : %d, Interrupt : %d",
		  qp->qid, qp->depth, qp->interrupt);

	// CQ-----------------------------
	snprintf(path, PATH_SIZE, "/dev/" LIBSTORAGE_CQ_PATH_FORMAT, nvme->id,
		 qp->qid);
	qp->cq_fd = open(path, O_RDWR);
	if (qp->cq_fd < 0) {
		LOG_WARN("Failed to open cq %s\n");
		return qp->cq_fd;
	}
	qp->cqes = mmap(NULL, qp->depth * sizeof(struct nvme_completion),
			PROT_READ | PROT_WRITE, MAP_SHARED, qp->cq_fd, 0);
	if (qp->cqes == MAP_FAILED) {
		LOG_WARN("Failed to mmap cq\n");
		return -1;
	}

	// SQ-----------------------------
	snprintf(path, PATH_SIZE, "/dev/" LIBSTORAGE_SQ_PATH_FORMAT, nvme->id,
		 qp->qid);
	qp->sq_fd = open(path, O_RDWR);
	if (qp->sq_fd < 0) {
		LOG_WARN("Failed to open sq\n");
		return qp->sq_fd;
	}
	qp->sqes = mmap(NULL, qp->depth * sizeof(struct nvme_command),
			PROT_READ | PROT_WRITE, MAP_SHARED, qp->sq_fd, 0);
	if (qp->sqes == MAP_FAILED) {
		LOG_WARN("Failed to mmap sq\n");
		return -1;
	}

	// DB-----------------------------
	snprintf(path, PATH_SIZE, "/dev/" LIBSTORAGE_DB_PATH_FORMAT, nvme->id,
		 qp->qid);
	qp->db_fd = open(path, O_RDWR);
	if (qp->db_fd < 0) {
		LOG_WARN("Failed to open db\n");
		return qp->db_fd;
	}
	qp->dbs = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       qp->db_fd, 0);
	if (qp->dbs == MAP_FAILED) {
		LOG_WARN("Failed to mmap db\n");
		return -1;
	}

	LOG_INFO("mmap all succeed, cqes : %p, sqes : %p, dbs : %p",
		 qp->cqes, qp->sqes, qp->dbs);
	q_dbs = qp->dbs + PAGE_SIZE;

	qp->sq_db = &q_dbs[qp->qid * 2 * nvme->db_stride];
	qp->cq_db = &q_dbs[(qp->qid * 2 + 1) * nvme->db_stride];

	qp->cq_head = 0;
	qp->sq_head = 0;
	qp->sq_tail = 0;
	qp->cq_phase = 1;

	qp->iods = calloc(qp->depth, sizeof(struct io_desc));

	LOG_DEBUG("Create QP %d Success", qp->qid);
	req_size_padded = (sizeof(io_request) + 63) & ~(size_t)63;
	// qp->req_buf = malloc(req_size_padded * args->num_requests);
	args->num_requests = min(args->num_requests, MAX_IO_REQS);
	qp->req_buf = calloc(args->num_requests, sizeof(io_request));
	qp->num_req = args->num_requests;
	// for (int i = 0; i < args->num_requests; i++) {
	// 	qp->req_ring_buffer[i] = (io_request *)((char *)qp->req_buf +
	// 						i * req_size_padded);
	// }
	// qp_node_t *qp_node = (qp_node_t *)malloc(sizeof(qp_node_t));
	// qp_node->qp = qp;
	// qp_node->next = g_qp_list;
	// g_qp_list = qp_node;
	LOG_DEBUG("TID : %d, qp : %p", gettid(), qp);
	local_qp = qp;
	LOG_DEBUG("Create qp req buffer Success. Return");
	return ret;
}
int delete_qp(ls_queue_args *args, ls_nvme_dev *nvme, ls_nvme_qp *qp)
{
	_clui();
	int ret = 0;

	munmap((void *)qp->cqes, qp->depth * sizeof(struct nvme_completion));
	close(qp->cq_fd);

	munmap((void *)qp->sqes, qp->depth * sizeof(struct nvme_command));
	close(qp->sq_fd);

	munmap((void *)qp->dbs, 2 * PAGE_SIZE);
	close(qp->db_fd);


	free(qp->iods);

	ret = ioctl_delete_qp(args);
	if (ret < 0) {
		LOG_WARN("Failed to delete qp\n");
	}
	// qp_node_t *prev = NULL;
	// qp_node_t *cur = g_qp_list;

	// while (cur) {
	// 	if (cur->qp == qp) {
	// 		if (prev) {
	// 			prev->next = cur->next;
	// 		} else {
	// 			g_qp_list = cur->next;
	// 		}
	// 		free(cur);
	// 		break;
	// 	}
	// 	prev = cur;
	// 	cur = cur->next;
	// }
	free(qp->req_buf);
	local_qp = NULL;
	memset(qp, 0, sizeof(ls_nvme_qp));
	_stui();
	return ret;
}

int create_dma_buffer(ls_nvme_dev *nvme, dma_buffer *buf, uint32_t size)
{
	int ret = 0;

	uint64_t va;
	uint64_t *pa;

	ret = alloc_dma_buffer(nvme->dma_pool, size, &va, &pa);
	if (ret < 0) {
		LOG_WARN("Failed to alloc dma buffer\n");
		return ret;
	}

	buf->size = size;
	buf->buf = (void *)va;
	buf->pa = pa;

	return ret;
}
int delete_dma_buffer(ls_nvme_dev *nvme, dma_buffer *buf)
{
	int ret = 0;
	free_dma_buffer(nvme->dma_pool, buf->buf, buf->size);
	return ret;
}

int ls_sched_yield(ls_device_args *args)
{
	ioctl_sched_yield(args);
}