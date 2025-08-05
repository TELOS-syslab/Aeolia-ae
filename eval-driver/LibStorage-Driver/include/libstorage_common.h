#ifndef LIBSTORAGE_COMMON_H
#define LIBSTORAGE_COMMON_H

// Common for driver & kernel module & user application
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#endif

#include "scx_eevdf.h"

#define PATH_SIZE 32

enum { LS_INTERRPUT_POLLING = 0, LS_INTERRPUT_UINTR };
typedef struct libstorage_queue_args {
	uint8_t nid;
	uint8_t qid;

	uint8_t interrupt;

	uint16_t depth;
	int32_t flags;
	uint32_t upid_idx;
	uint8_t instance_id;
	uint32_t num_requests;
} ls_queue_args;

typedef struct libstorage_device_args {
	uint8_t nid;
	char path[32];

	uint32_t max_hw_sectors;
	uint32_t db_stride;

	int32_t lba_shift;
	uint32_t nsid;

	uint8_t instance_id;
	int tid;
	void (*uintr_handler)(void);
	uint8_t upid_idx;

	uint8_t ioclass;
} ls_device_args;

typedef struct libstorage_set_prio_args {
	uint8_t nid;
	uint32_t l_weight;
	uint32_t m_weight;
	uint32_t h_weight;
} ls_set_prio_args;

typedef struct uintr_upid {
	struct {
		uint8_t status; /* bit 0: ON, bit 1: SN, bit 2-7: reserved */
		uint8_t reserved1; /* Reserved */
		uint8_t nv; /* Notification vector */
		uint8_t reserved2; /* Reserved */
		uint8_t ndst; /* Notification destination */
	} __attribute__((packed)) nc; /* Notification control */
	uint64_t puir; /* Posted user interrupt requests */
} __attribute__((aligned(64))) uintr_upid;

#endif // LIBSTORAGE_COMMON_H