#ifndef SUFS_KFS_LEASE_H_
#define SUFS_KFS_LEASE_H_

#include <linux/sched.h>

#include "../include/kfs_config.h"

struct sufs_kfs_lease {
    spinlock_t lock;
    int state;
    int owner_cnt;
    pid_t owner[SUFS_KFS_LEASE_MAX_OWNER];
};

int sufs_kfs_lease_fini(void);

int sufs_kfs_lease_init(void);

int sufs_kfs_mmap_lease(int ino, int writable);

int sufs_kfs_munmap_lease(int ino);

int sufs_kfs_notify_others_sharing(int ino);
int sufs_kfs_notify_others_private(int ino);

#endif /* SUFS_KFS_LEASE_H_ */
