#ifndef TFS_RWLOCK_BRAVO_H_
#define TFS_RWLOCK_BRAVO_H_

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "trwlock.h"

struct tfs_bravo_rwlock {
    bool rbias;
    unsigned long inhibit_until;
    tfs_rwticket underlying;
};

static inline void tfs_bravo_rwlock_init(struct tfs_bravo_rwlock *l) {
    l->rbias = true;
    l->inhibit_until = 0;
    l->underlying.u = 0;
}

static void inline tfs_bravo_rwlock_destroy(struct tfs_bravo_rwlock *l) {}

static void inline tfs_bravo_write_unlock(struct tfs_bravo_rwlock *l) {
    tfs_rwticket_wrunlock(&l->underlying);
}

void tfs_bravo_read_lock(struct tfs_bravo_rwlock *l);
void tfs_bravo_read_unlock(struct tfs_bravo_rwlock *l);

void tfs_bravo_write_lock(struct tfs_bravo_rwlock *l);

#endif /* BRAVO_H */
