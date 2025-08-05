#include "fio_time.h"
#include "libstorage_api.h"
#include "libstorage_common.h"
#include <bits/time.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>
#include <x86gprintrin.h>
#include "file.h"
#include "fio.h"
#include "optgroup.h"
#include "io_ddir.h"
#include "io_u.h"
#include "ioengines.h"
#include <stdatomic.h>

struct ls_fio_options {
	void *pad;
	int intr;
	int type;
	int coalescing;
};
#if 1
#define FUNC_DEBUG() \
	fprintf(stdout, "TID: %d, %s Called\n", gettid(), __func__);
#define debugf(fmt, ...)                                                      \
	fprintf(stdout, "\033[0;34m[LS_FIO] [%s] : " fmt "\033[0m", __func__, \
		##__VA_ARGS__);

// #define debugf(fmt, ...)                                                   \
    //     _clui();                                                               \
    //     printf("\033[0;34m[LS_FIO] [%s] : " fmt "\033[0m", __func__, ##__VA_ARGS__);          \
    //     _stui();
#else
#define FUNC_DEBUG()
#define debugf
#endif

typedef struct {
	ls_nvme_qp *qp;
	ls_nvme_dev *nvme;
	ls_queue_args *qp_args;
	ls_device_args *dev_args;

	struct io_u **iocq;
	int head;
	int tail;
	atomic_int cbc;
} ls_io_thread_t;

void *init_frame_address;
typedef struct {
	struct thread_data *td;
	struct io_u *io_u;

	struct dma_buffer *dma_buffer;
} ls_iou_t;

extern __thread ls_nvme_qp *local_qp;
extern int g_interrupt_coalescing;
extern __thread int need_bubble;
atomic_int g_barrier_sync = ATOMIC_VAR_INIT(0);
static int fio_ls_setup(struct thread_data *td)
{
	FUNC_DEBUG();
	ls_io_thread_t *thread_handle = malloc(sizeof(ls_io_thread_t));
	ls_nvme_dev *nvme = malloc(sizeof(ls_nvme_dev));
	ls_nvme_qp *qp = malloc(sizeof(ls_nvme_qp));
	ls_queue_args *qp_args = malloc(sizeof(ls_queue_args));
	ls_device_args *dev_args = malloc(sizeof(ls_device_args));
	struct fio_file *f;
	int i;
	int result;
	int max_bs, iodepth;
	memset(thread_handle, 0, sizeof(ls_io_thread_t));
	memset(nvme, 0, sizeof(ls_nvme_dev));
	memset(qp, 0, sizeof(ls_nvme_qp));
	memset(qp_args, 0, sizeof(ls_queue_args));
	memset(dev_args, 0, sizeof(ls_device_args));

	for_each_file(td, f, i)
	{
		f->real_file_size = 40008845721;
		fio_file_set_size_known(f);
		strcpy(dev_args->path, f->file_name);
		break;
	}
	dev_args->tid = td->pid;
	max_bs = td_max_bs(td);
	iodepth = td->o.iodepth;
	result = open_device(dev_args, nvme);
	if (result < 0) {
		printf("Failed to open device\n");
		return -1;
	}
	// Create queue pair
	struct ls_fio_options *opts = (struct ls_fio_options *)td->eo;
	qp_args->nid = nvme->id;
	qp_args->interrupt = opts->intr;
	qp_args->depth = 4;
	// qp_args->num_requests = (((max_bs + 131072) / 131072) + 1) * iodepth;
	qp_args->num_requests = 1024;
	qp_args->instance_id = dev_args->instance_id;
	dev_args->ioclass = opts->type;
	if (opts->type) {
		qp_args->flags |= NVME_SQ_PRIO_URGENT;
	} else {
		qp_args->flags |= NVME_SQ_PRIO_LOW;
		// if (opts->intr == LS_INTERRPUT_UINTR) {
		// 	qp_args->depth =
		// 		max(iodepth, (256 * 1024 / max_bs) + 1);
		// }
	}
	g_interrupt_coalescing = opts->coalescing;
	result = create_qp(qp_args, nvme, qp);
	thread_handle->nvme = nvme;
	thread_handle->qp = qp;
	thread_handle->qp_args = qp_args;
	thread_handle->dev_args = dev_args;
	// atomic_init(&thread_handle->cbc, 0);
	thread_handle->cbc = ATOMIC_VAR_INIT(0);
	thread_handle->iocq = calloc(td->o.iodepth + 1, sizeof(void *));
	td->io_ops_data = (void *)thread_handle;
	return 0;
}
static int fio_ls_init(struct thread_data *td)
{
	FUNC_DEBUG()
	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	int ret = register_uintr(thread_handle->dev_args, thread_handle->nvme);
	local_qp = thread_handle->qp;
	return ret;
}
static void fio_ls_cleanup(struct thread_data *td)
{
	// FUNC_DEBUG()
	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	int result;
	result = delete_qp(thread_handle->qp_args, thread_handle->nvme,
			   thread_handle->qp);
	result = close_device(thread_handle->dev_args, thread_handle->nvme);
}

static struct io_u *fio_ls_event(struct thread_data *td, int event)
{
	// FUNC_DEBUG()

	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	struct io_u *io_u = thread_handle->iocq[thread_handle->head];
	if (++thread_handle->head == td->o.iodepth)
		thread_handle->head = 0;

	return io_u;
}

static int fio_ls_getevents(struct thread_data *td, unsigned int min,
			    unsigned int max, const struct timespec *t)
{
	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	int events = 0, ret = 0;
	struct timespec t0, t1;
	uint64_t timeout = 0;

	if (t) {
		timeout = t->tv_sec * 1000000000L + t->tv_nsec;
		clock_gettime(CLOCK_MONOTONIC, &t0);
	}

	while (true) {
		_clui();
		if (thread_handle->qp_args->interrupt == LS_INTERRPUT_POLLING) {
			ls_qpair_process_completions(local_qp, 0);
		}

		while (atomic_load(&thread_handle->cbc) > 0) {
			atomic_fetch_sub(&thread_handle->cbc, 1);
			events++;
		}
		if (events >= min) {
			_stui();
			return events;
		}
		if (((struct ls_fio_options *)td->eo)->intr == 1 &&
		    ((struct ls_fio_options *)td->eo)->type == 1) {
			ls_sched_yield(thread_handle->dev_args);
			ls_qpair_process_completions(local_qp, 0);
		}
		_stui();
		if (t) {
			clock_gettime(CLOCK_MONOTONIC, &t1);
			uint64_t elapsed =
				(t1.tv_sec - t0.tv_sec) * 1000000000L +
				t1.tv_nsec - t0.tv_nsec;
			if (elapsed > timeout)
				break;
		}
	}
	_stui();
	return events;
}
static void aio_callback(void *data)
{
	struct io_u *io_u = (struct io_u *)data;
	ls_iou_t *lsiou = (ls_iou_t *)io_u->engine_data;
	ls_io_thread_t *thread_handler = lsiou->td->io_ops_data;
	thread_handler->iocq[thread_handler->tail] = io_u;
	if (++thread_handler->tail == lsiou->td->o.iodepth)
		thread_handler->tail = 0;
	atomic_fetch_add(&thread_handler->cbc, 1);
}
__thread int start;
static enum fio_q_status fio_ls_queue(struct thread_data *td, struct io_u *io_u)
{
	if (!start) {
		start = 1;
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		printf("TID: %d, Queue Time at %ld.%09ld\n", gettid(),
		       ts.tv_sec, ts.tv_nsec);
	}
	// FUNC_DEBUG()
	fio_ro_check(td, io_u);
	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	ls_iou_t *lsiou = (ls_iou_t *)io_u->engine_data;
	int res = 0, is_sync = 1;
	// if (thread_handle->qp->nvme->upid->puir == 128) {
	// 	sched_yield();
	// }
	// _clui();
	switch (io_u->ddir) {
	case DDIR_READ:
		res = read_blk_async(thread_handle->qp, io_u->offset,
				     io_u->xfer_buflen, lsiou->dma_buffer,
				     aio_callback, io_u);

		break;
	case DDIR_WRITE:
		res = write_blk_async(thread_handle->qp, io_u->offset,
				      io_u->xfer_buflen, lsiou->dma_buffer,
				      aio_callback, io_u);
		break;
	default:
		break;
	}
	// _stui();
	// printf("%d\n", res);
	// if (thread_handle->qp->interrupt)
	// 	ls_sched_yield(thread_handle->dev_args);
	return FIO_Q_QUEUED;
	// return res == IO_COMPLETE ? (is_sync ? FIO_Q_COMPLETED : FIO_Q_QUEUED) : FIO_Q_BUSY;
}
static int fio_ls_io_u_init(struct thread_data *td, struct io_u *io_u)
{
	FUNC_DEBUG()
	ls_iou_t *lsiou = malloc(sizeof(ls_iou_t));
	lsiou->td = td;
	lsiou->io_u = io_u;
	ls_io_thread_t *thread_handle = (ls_io_thread_t *)td->io_ops_data;
	lsiou->dma_buffer = malloc(sizeof(struct dma_buffer));
	int size = max(td_max_bs(td), 4096llu);
	int res =
		create_dma_buffer(thread_handle->nvme, lsiou->dma_buffer, size);
	if (res < 0) {
		return 1;
	}
	io_u->engine_data = lsiou;
	return 0;
}

static void fio_ls_io_u_free(struct thread_data *td, struct io_u *io_u)
{
	ls_iou_t *lsiou = (ls_iou_t *)io_u->engine_data;
	delete_dma_buffer(((ls_io_thread_t *)td->io_ops_data)->nvme,
			  lsiou->dma_buffer);
	free(lsiou);
	io_u->engine_data = NULL;
}

static int fio_ls_get_file_size(struct thread_data *td, struct fio_file *f)
{
	// FUNC_DEBUG()
	f->filetype = FIO_TYPE_CHAR;
	f->real_file_size = 10ll * 1024 * 1024 * 1024;
	fio_file_set_size_known(f);
	return 0;
}
static int fio_ls_open_file(struct thread_data *td, struct fio_file *f)
{
	// FUNC_DEBUG()
	return 0;
}
static int fio_ls_close_file(struct thread_data *td, struct fio_file *f)
{
	// FUNC_DEBUG()
	return 0;
}

static struct fio_option options[] = {
	{
		.name = "intr",
		.lname = "Interrupt",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct ls_fio_options, intr),
		.help = "Interrupt mode",
		.category = FIO_OPT_C_ENGINE,
	},
	{
		.name = "type",
		.lname = "app_type",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct ls_fio_options, type),
		.help = "app mode",
		.category = FIO_OPT_C_ENGINE,
	},
	{
		.name = "coalescing",
		.lname = "interrupt_coalescing",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct ls_fio_options, coalescing),
		.help = "app mode",
		.category = FIO_OPT_C_ENGINE,
	},
	{
		.name = NULL,
	},
};
struct ioengine_ops ioengine = {
	.name = "libstorage",
	.version = FIO_IOOPS_VERSION,
	.init = fio_ls_init,
	.setup = fio_ls_setup,
	.cleanup = fio_ls_cleanup,
	.queue = fio_ls_queue,
	.getevents = fio_ls_getevents,
	.event = fio_ls_event,
	.open_file = fio_ls_open_file,
	.close_file = fio_ls_close_file,
	.io_u_init = fio_ls_io_u_init,
	.io_u_free = fio_ls_io_u_free,
	.flags = FIO_RAWIO | FIO_NOEXTEND | FIO_NODISKUTIL | FIO_MEMALIGN |
		 FIO_DISKLESSIO,
	.options = options,
	.option_struct_size = sizeof(struct ls_fio_options),
};
static void fio_init fio_ls_register(void)
{
	printf("Registering LS IO engine\n");
	register_ioengine(&ioengine);
}
static void fio_exit fio_ls_unregister(void)
{
	unregister_ioengine(&ioengine);
}
