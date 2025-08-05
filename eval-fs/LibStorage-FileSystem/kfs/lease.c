#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include "../include/kfs_config.h"
#include "lease.h"
#include "super.h"
#include "util.h"

#define SUFS_KFS_UNOWNED 0
#define SUFS_KFS_READ 1
#define SUFS_KFS_WRITE 2
#define SUFS_KFS_READ_WRITE 3

void *sufs_global_lease = NULL;

int sufs_kfs_lease_fini(void) {
    vfree(sufs_global_lease);
    return 0;
}

// FIXME: the global lease is so big...
int sufs_kfs_lease_init(void) {
    sufs_global_lease =
        vzalloc(SUFS_MAX_INODE_NUM * sizeof(struct sufs_kfs_lease));
    if (!sufs_global_lease) {
        WARN_FS("sufs_kfs_lease_init: failed to allocate memory\n");
        return -ENOMEM;
    }
    return 0;
}

int sufs_kfs_mmap_lease(int ino, int writable) {
    int ret = 0;
    unsigned long flags;
    struct sufs_kfs_lease *lease =
        &(((struct sufs_kfs_lease *)sufs_global_lease)[ino]);

    spin_lock_irqsave(&(lease->lock), flags);

    if (writable) {
        if (lease->state == SUFS_KFS_UNOWNED) {
            lease->state = SUFS_KFS_WRITE;
            lease->owner_cnt = 1;
            lease->owner[0] = (current->tgid << 1) | 1;
            ret = 0;
        } else if (lease->state == SUFS_KFS_READ ||
                   lease->state == SUFS_KFS_WRITE) {
            lease->state = SUFS_KFS_READ_WRITE;
            lease->owner_cnt++;
            if (lease->owner_cnt > SUFS_KFS_LEASE_MAX_OWNER) {
                WARN_FS("sufs_kfs_mmap_lease: too many owners\n");
                ret = -EBUSY;
            } else {
                lease->owner[lease->owner_cnt - 1] = (current->tgid << 1) | 1;
                ret = 1;
            }
            sufs_kfs_notify_others_sharing(ino);
        } else if (lease->state == SUFS_KFS_READ_WRITE) {
            lease->state = SUFS_KFS_READ_WRITE;
            lease->owner_cnt++;
            if (lease->owner_cnt > SUFS_KFS_LEASE_MAX_OWNER) {
                WARN_FS("sufs_kfs_mmap_lease: too many owners\n");
                ret = -EBUSY;
            } else {
                lease->owner[lease->owner_cnt - 1] = (current->tgid << 1) | 1;
                ret = 1;
            }
        } else {
            WARN_FS("sufs_kfs_mmap_lease: invalid state\n");
            ret = -EINVAL;
        }
    } else {
        if (lease->state == SUFS_KFS_UNOWNED) {
            lease->state = SUFS_KFS_READ;
            lease->owner_cnt = 1;
            lease->owner[0] = (current->tgid << 1);
            ret = 0;
        } else if (lease->state == SUFS_KFS_WRITE) {
            lease->state = SUFS_KFS_READ_WRITE;
            lease->owner_cnt++;
            if (lease->owner_cnt > SUFS_KFS_LEASE_MAX_OWNER) {
                WARN_FS("sufs_kfs_mmap_lease: too many owners\n");
                ret = -EBUSY;
            } else {
                lease->owner[lease->owner_cnt - 1] = (current->tgid << 1);
                ret = 1;
            }
            sufs_kfs_notify_others_sharing(ino);
        } else if (lease->state == SUFS_KFS_READ ||
                   lease->state == SUFS_KFS_READ_WRITE) {
            lease->state = SUFS_KFS_READ;
            lease->owner_cnt++;
            if (lease->owner_cnt > SUFS_KFS_LEASE_MAX_OWNER) {
                WARN_FS("sufs_kfs_mmap_lease: too many owners\n");
                ret = -EBUSY;
            } else {
                lease->owner[lease->owner_cnt - 1] = (current->tgid << 1);
                ret = (lease->state == SUFS_KFS_READ ? 0 : 1);
            }
        } else {
            WARN_FS("sufs_kfs_mmap_lease: invalid state\n");
            ret = -EINVAL;
        }
    }

    spin_unlock_irqrestore(&(lease->lock), flags);
    return ret;
}

static void sufs_kfs_gc_lease(struct sufs_kfs_lease *l) {
    int i = 0;

    pid_t sowner[SUFS_KFS_LEASE_MAX_OWNER];
    int sowner_cnt = 0;

    for (i = 0; i < l->owner_cnt; i++) {
        if (l->owner[i] != 0) {
            sowner[sowner_cnt] = l->owner[i];
            sowner_cnt++;
        }
    }

    for (i = 0; i < sowner_cnt; i++) {
        l->owner[i] = sowner[i];
    }

    l->owner_cnt = sowner_cnt;
}

static int sufs_kfs_is_acquired_lock(struct sufs_kfs_lease *l, int tgid,
                                     int *index) {

    int i = 0;
    for (i = 0; i < l->owner_cnt; i++) {
        if (tgid == (l->owner[i] >> 1)) {
            if (index)
                (*index) = i;
            return 1;
        }
    }

    return 0;
}

int sufs_kfs_munmap_lease(int ino) {
    unsigned long flags;
    struct sufs_kfs_lease *l =
        &(((struct sufs_kfs_lease *)sufs_global_lease)[ino]);
    int index;
    int ret = 0;
    spin_lock_irqsave(&(l->lock), flags);

    /* check whether the lease has been acquired by the current trust group */
    if (!sufs_kfs_is_acquired_lock(l, current->tgid, &index)) {
        ret = -EINVAL;
        goto out;
    }

    if (l->state == SUFS_KFS_UNOWNED) {
        WARN_FS("sufs_kfs_munmap_lease: invalid state\n");
        spin_unlock_irqrestore(&(l->lock), flags);
        return -EINVAL;
    }

    l->owner[index] = 0;
    sufs_kfs_gc_lease(l);

    if (l->owner_cnt == 0) {
        l->state = SUFS_KFS_UNOWNED;
    }

    // we don't consider multi reader.
    if (l->owner_cnt == 1 && l->state == SUFS_KFS_READ_WRITE) {
        int write = l->owner[0] & 1;
        if (write) {
            l->state = SUFS_KFS_WRITE;
        } else {
            l->state = SUFS_KFS_READ;
        }
        sufs_kfs_notify_others_private(ino);
    }
out:
    spin_unlock_irqrestore(&(l->lock), flags);
    return ret;
}

int sufs_kfs_notify_others_sharing(int ino) {
    WARN_FS("sufs_kfs_notify_others: not implemented\n");
    // TODO
    return 0;
}

int sufs_kfs_notify_others_private(int ino) {
    WARN_FS("sufs_kfs_notify_others: not implemented\n");
    // TODO
    return 0;
}