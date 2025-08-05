#include "command_types.h"
#include "libstorage.h"
#include "libstorage_api.h"
#include "logger.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <unistd.h>

static int total_reqs = 0;
static int received_reqs = 0;
static inline io_request *ls_alloc_req(ls_nvme_qp *qp);
static inline void ls_free_req(io_request *req);
static io_request *ls_prepare_io_request(ls_nvme_qp *qp, uint64_t lba,
					 uint32_t lba_count, void *buf,
					 void *pa_list,
					 void (*callback)(void *), void *data,
					 int opcode);
static inline int nvme_complete_iod(ls_nvme_qp *qp, io_desc *iod);
static inline int ls_cb_complete_child(void *child_arg);
void *g_last_rip;
__thread int g_interrupt_times = 0;
__thread int g_submit, g_complete;
int g_interrupt_coalescing;
extern __thread ls_nvme_qp *local_qp;

uint64_t g_start_time = 0;
uint64_t g_end_time = 0;
// extern ls_nvme_qp *local_qp;
void __attribute__((interrupt))
ls_nvme_irq_user_handler(struct __uintr_frame *ui_frame, uint64_t vector)
{
	ls_qpair_process_completions(local_qp, 0);
}

inline int ls_qpair_process_completions(ls_nvme_qp *qp, int maxn)
{
	int ret = 0;
	io_desc *iod;
	io_request *req;
	uint16_t head = qp->cq_head, phase = qp->cq_phase;
	volatile struct nvme_completion *cqe;
	struct nvme_completion cqe_tmp;
	// We can exit only found = 0 or a parent completed
	int found = 0, exit = 0, tot = 0;
	if (qp->interrupt == LS_INTERRPUT_UINTR) {
		qp->nvme->upid->puir = 1;
	}
	while (1) {
		cqe = (volatile struct nvme_completion *)&qp->cqes[head];
		cqe_tmp = *cqe;
		if (cqe->status >> 1) {
			printf("CQE status : %x\n", cqe->status);
			assert(0);
		}
		if ((cqe->status & 1) != phase) {
			return 0;
		}
		g_end_time = _rdtsc();
		printf("[RECORD DEVICE LATENCY] : %lu\n",
		       g_end_time - g_start_time);
		exit = 0;
		iod = &qp->iods[cqe->command_id];
		if (++head == qp->depth) {
			head = 0;
			phase ^= 1;
		}
		g_complete++;
		int lst = nvme_complete_iod(qp, iod);
		__sync_synchronize();
		*(volatile uint32_t *)qp->cq_db = head;
		qp->cq_head = head;
		qp->cq_phase = phase;
	}
	return ret;
}
static inline int nvme_complete_iod(ls_nvme_qp *qp, io_desc *iod)
{
	io_request *req = iod->req;
	if (iod->prp_addr != NULL) {
		free_prp_buf(qp->nvme, iod->prp_addr);
	}
	iod->status = IO_COMPLETE;
	// LOG_WARN("Complete IOD : %d, req : %p", iod->id, iod->req);
	if (!req->parent) {
		if (req->callback) {
			req->callback(req->data);
		}
		ls_free_req(req);
		return 0;
	} else {
		return ls_cb_complete_child(req);
	}
}
static inline int ls_cb_complete_child(void *child_arg)
{
	io_request *child = (io_request *)child_arg;
	assert(child != NULL);
	io_request *parent = child->parent;
	if (parent) {
		parent->num_children--;
		parent->submit_children--;
		child->parent = NULL;
		TAILQ_REMOVE(&parent->children, child, child_tailq);
	}
	ls_free_req(child);

	if (parent->num_children == 0) {
		if (parent->callback) {
			parent->callback(parent->data);
		}
		ls_free_req(parent);
		return 0;
	}
	return parent->submit_children;
}
static void ls_sync_default_callback(void *data)
{
	received_reqs++;
}
// ******************************* IO_REQ FUNCTIONS *******************************

int g_alloc = 0;
int g_free = 0;
void *last_alloc, *last_free;
__thread int need_bubble;
static inline io_request *ls_alloc_req(ls_nvme_qp *qp)
{
	io_request *req;
	int failed = 0;
	while (1) {
		req = &qp->req_buf[qp->req_pos++];
		if (qp->req_pos >= qp->num_req) {
			qp->req_pos = 0;
		}
		if (req->flag == 0) {
			req->flag = 1;
			return req;
		}
	}
	return req;
}
static inline void ls_free_req(io_request *req)
{
	assert(req != NULL);
	assert(req->num_children == 0);
	assert(req->qp != NULL);
	req->flag = 0;
	// fprintf(stderr, "TID : %d free req %p\n", gettid(), req);
}
int temp_gdb_flag1 = 0;
static int ls_submit_req(ls_nvme_qp *qp, io_request *req)
{
	if (req->num_children) {
		LOG_DEBUG("Submit a req with children %d", req->num_children);
		// submit each children;
		io_request *child;
		TAILQ_FOREACH(child, &req->children, child_tailq)
		{
			ls_submit_req(qp, child);
			req->submit_children++;
		}
		return 0;
	}
	int iod_id;
	io_desc *iod = NULL;
	ls_nvme_dev *nvme = qp->nvme;
	struct nvme_command *cmd;
	while (1) {
		iod_id = qp->iod_pos++;
		iod = &qp->iods[iod_id];
		if (qp->iod_pos >= qp->depth) {
			qp->iod_pos = 0;
		}
		if (iod->status != IO_INITED) {
			break;
		}
		if (temp_gdb_flag1) {
			ls_qpair_process_completions(qp, 1);
		}
	}

	iod->status = IO_INITED;
	iod->sq_tail = qp->sq_tail;
	iod->id = iod_id;
	iod->prp_addr = req->prp2_addr;
	iod->req = req;
	cmd = &qp->sqes[qp->sq_tail];
	g_submit++;
	memset(cmd, 0, sizeof(struct nvme_command));
	switch (req->opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
		cmd->rw.opcode = req->opcode;
		cmd->rw.command_id = iod_id;
		cmd->rw.nsid = nvme->nsid;
		cmd->rw.prp1 = req->prp1;
		cmd->rw.prp2 = req->prp2;
		cmd->rw.slba = req->lba >> nvme->lba_shift;
		cmd->rw.length = (req->lba_count >> nvme->lba_shift) - 1;
		cmd->rw.control = 0;
		cmd->rw.dsmgmt = 0;
		break;
	default:
		LOG_ERROR("Invalid opcode");
		return -EINVAL;
	}
	// LOG_DEBUG("CMD : %d-%d-%d-%llx-%llx-%llx-%d", cmd->common.opcode,
	// 	  cmd->common.command_id, cmd->common.nsid, cmd->rw.prp1,
	// 	  cmd->rw.prp2, cmd->rw.slba, cmd->rw.length);
	if (++qp->sq_tail >= qp->depth) {
		qp->sq_tail = 0;
	}
	__sync_synchronize();
	*(volatile uint32_t *)qp->sq_db = qp->sq_tail;
	g_start_time = _rdtsc();
	LOG_DEBUG(
		"Submit a req with cmd_id : %d, opcode : %d, now tail : %d, now puir : %lu",
		iod_id, req->opcode, qp->sq_tail, nvme->upid->puir);
	// getchar();
	return 0;
}
static inline void ls_setup_io_request(ls_nvme_qp *qp, io_request *req,
				       uint64_t lba, uint32_t lba_count,
				       void *buf, void *paBase, int opcode)
{
	uint64_t prp1, prp2;
	void *prp2_addr = NULL;
	ls_nvme_dev *nvme = qp->nvme;
	make_prp_list(nvme, buf, lba_count, paBase, &req->prp1, &req->prp2,
		      &req->prp2_addr);
	req->opcode = opcode;
	return;
}

static io_request *split_io_request(ls_nvme_qp *qp, io_request *req,
				    uint64_t lba, uint32_t lba_count, void *buf,
				    uint64_t *pa_list, int opcode)
{
	uint32_t max_sz_per_io = qp->nvme->max_hw_sectors * BLK_SIZE;
	// LOG_INFO("Max sz per io : %d", max_sz_per_io);
	uint32_t remaining_lba_count = req->lba_count;
	io_request *child;
	if (req->num_children == 0) {
		TAILQ_INIT(&req->children);
		req->parent = NULL;
	}

	while (remaining_lba_count > 0) {
		uint32_t lba_count = min(remaining_lba_count, max_sz_per_io);
		child = ls_prepare_io_request(qp, lba, lba_count, buf, pa_list,
					      NULL, NULL, opcode);
		if (!child) {
			LOG_ERROR("Failed to allocate io request");
			return NULL;
		}
		child->parent = req;
		TAILQ_INSERT_TAIL(&req->children, child, child_tailq);
		req->num_children++;
		remaining_lba_count -= lba_count;
		lba += lba_count;
		buf += lba_count;
		pa_list += (lba_count + PAGE_SIZE - 1) / PAGE_SIZE;
	}
	return req;
}

static io_request *ls_prepare_io_request(ls_nvme_qp *qp, uint64_t lba,
					 uint32_t lba_count, void *buf,
					 void *pa_list,
					 void (*callback)(void *), void *data,
					 int opcode)
{
	uint32_t max_sz_per_io = qp->nvme->max_hw_sectors * BLK_SIZE;
	// uint32_t max_sz_per_io = 8 * BLK_SIZE;

	io_request *req = NULL;
	req = ls_alloc_req(qp);
	if (!req) {
		LOG_ERROR("Failed to allocate io request");
		return NULL;
	}
	// memset(req, 0, sizeof(io_request));
	req->callback = callback;
	req->data = data;
	req->lba = lba;
	req->lba_count = lba_count;
	req->qp = qp;
	if (lba_count > max_sz_per_io) {
		return split_io_request(qp, req, lba, lba_count, buf, pa_list,
					opcode);
	}
	ls_setup_io_request(qp, req, lba, lba_count, buf, pa_list, opcode);
	LOG_DEBUG(
		"Prepared IO Request : %p, lba : %lu, lba_count : %u, buf : %p, prp1 : %lx, prp2 : %lx",
		req, req->lba, req->lba_count, buf, req->prp1, req->prp2);
	return req;
}
int read_blk(ls_nvme_qp *qp, uint64_t lba, uint32_t count, dma_buffer *buf)
{
	int ret = 0;
	total_reqs++;
	read_blk_async(qp, lba, count, buf, ls_sync_default_callback, NULL);
	LOG_DEBUG("Sync API Start Polling");
	while (received_reqs < total_reqs) {
		if (qp->interrupt != LS_INTERRPUT_UINTR)
			ls_qpair_process_completions(qp, 0);
	}
	return ret;
}
int write_blk(ls_nvme_qp *qp, uint64_t lba, uint32_t count, dma_buffer *buf)
{
	int ret = 0;
	total_reqs++;
	write_blk_async(qp, lba, count, buf, ls_sync_default_callback, NULL);
	while (received_reqs < total_reqs) {
		if (qp->interrupt != LS_INTERRPUT_UINTR)
			ls_qpair_process_completions(qp, 0);
	}
	return ret;
}
int read_blk_async(ls_nvme_qp *qp, uint64_t lba, uint32_t lba_count,
		   dma_buffer *buf, void (*callback)(void *), void *data)
{
	int ret = 0;
	io_request *req = ls_prepare_io_request(qp, lba, lba_count, buf->buf,
						buf->pa, callback, data,
						nvme_cmd_read);
	if (!req) {
		LOG_ERROR("Failed to prepare io request");
		return -ENOMEM;
	}
	ret = ls_submit_req(qp, req);
	return ret;
}
int write_blk_async(ls_nvme_qp *qp, uint64_t lba, uint32_t lba_count,
		    dma_buffer *buf, void (*callback)(void *), void *data)
{
	int ret = 0;
	io_request *req = ls_prepare_io_request(qp, lba, lba_count, buf->buf,
						buf->pa, callback, data,
						nvme_cmd_write);
	if (!req) {
		LOG_ERROR("Failed to prepare io request");
		return -ENOMEM;
	}
	LOG_DEBUG("Prepared IO Request : %p, child : %d", req,
		  req->num_children);
	ret = ls_submit_req(qp, req);
	return ret;
}