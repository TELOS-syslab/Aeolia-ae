#ifndef LIBSTORAGE_H
#define LIBSTORAGE_H

#include "libstorage_api.h"
#include "libstorage_common.h"
#include "command_types.h"
#include "logger.h"

#include <x86gprintrin.h>
#include <sys/queue.h>
#include <stdint.h>
#include <sys/errno.h>
#define IO_REQUEST_SYNC	 0
#define IO_REQUEST_ASYNC 1

#define BLK_SIZE 512

typedef struct qp_node {
	ls_nvme_qp *qp;
	struct qp_node *next;
} qp_node_t;

// extern qp_node_t *g_qp_list;
#define min(a, b) ((a) < (b) ? (a) : (b))
typedef struct prp_page_list {
	uint64_t bitmap[8];
	uint64_t va;
	uint64_t *pa;
	struct prp_page_list *next;
} prp_page_list;

enum {
	IO_INITED = 1 << 0,
	IO_COMPLETE = 1 << 1,
	IO_ERROR = 1 << 2,

	IO_NUM_FLAGS = 3,
};
typedef struct io_desc {
	uint16_t sq_tail;
	uint16_t status;
	void *prp_addr;

	uint16_t id;

	io_request *req;
} io_desc;

void ls_nvme_irq_user_handler(struct __uintr_frame *ui_frame, uint64_t vector);

void *free_prp_buf(ls_nvme_dev *nvme, void *buf);
int make_prp_list(ls_nvme_dev *nvme, void *buf, uint32_t iosize,
		  uint64_t *paBase, uint64_t *prp1, uint64_t *prp2,
		  void **prp2_addr);

#endif // LIBSTORAGE_H