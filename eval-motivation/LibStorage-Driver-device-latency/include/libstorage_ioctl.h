#ifndef LIBSTORAGE_IOCTL_H
#define LIBSTORAGE_IOCTL_H

#define LIBSTORAGE_MAGIC	'L'
#define LIBSTORAGE_OPEN_DEVICE	_IOWR(LIBSTORAGE_MAGIC, 0, uint64_t)
#define LIBSTORAGE_CLOSE_DEVICE _IOWR(LIBSTORAGE_MAGIC, 1, uint64_t)
#define LIBSTORAGE_REG_UINTR	_IOWR(LIBSTORAGE_MAGIC, 2, uint64_t)
#define LIBSTORAGE_CREATE_QP	_IOWR(LIBSTORAGE_MAGIC, 10, uint64_t)
#define LIBSTORAGE_DELETE_QP	_IOWR(LIBSTORAGE_MAGIC, 11, uint64_t)
#define LIBSTORAGE_SET_PRIORITY _IOWR(LIBSTORAGE_MAGIC, 20, uint64_t)

#define LIBSTORAGE_SCHED_YIELD _IOWR(LIBSTORAGE_MAGIC, 30, uint64_t)

#define LIBSTORAGE_UPID_PATH_FORMAT  "ls_nvme/nvme%dinstance%d"
#define LIBSTORAGE_SQ_PATH_FORMAT    "ls_nvme_qps/nvme%dsq%d"
#define LIBSTORAGE_CQ_PATH_FORMAT    "ls_nvme_qps/nvme%dcq%d"
#define LIBSTORAGE_DB_PATH_FORMAT    "ls_nvme_qps/nvme%ddb%d"

#define DEVICE_NAME "libdriver"
#include "libstorage_common.h"

#define ioctl_open_device(args)	   ioctl(ls_dev_fd, LIBSTORAGE_OPEN_DEVICE, args)
#define ioctl_close_device(args)   ioctl(ls_dev_fd, LIBSTORAGE_CLOSE_DEVICE, args)
#define ioctl_register_uintr(args) ioctl(ls_dev_fd, LIBSTORAGE_REG_UINTR, args)
#define ioctl_create_qp(args)	   ioctl(ls_dev_fd, LIBSTORAGE_CREATE_QP, args)
#define ioctl_delete_qp(args)	   ioctl(ls_dev_fd, LIBSTORAGE_DELETE_QP, args)

#define ioctl_set_priority(args) ioctl(ls_dev_fd, LIBSTORAGE_SET_PRIORITY, args)
#define ioctl_sched_yield(args)	 ioctl(ls_dev_fd, LIBSTORAGE_SCHED_YIELD, args)
#endif // LIBSTORAGE_IOCTL_H