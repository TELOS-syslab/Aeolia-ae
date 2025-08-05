#ifndef TFS_TRWLOCK_H_
#define TFS_TRWLOCK_H_

#include "atomic_util.h"
#include "compiler.h"
#include "libfs-headers.h"

typedef union tfs_rwticket tfs_rwticket;

union tfs_rwticket {
    unsigned int u;
    unsigned short us;
    __extension__ struct {
        unsigned char write;
        unsigned char read;
        unsigned char users;
    } s;
};

static inline void tfs_rwticket_wrlock(tfs_rwticket *l) {
    unsigned int me = tfs_atomic_xadd(&l->u, (1 << 16));
    unsigned char val = me >> 16;

    while (val != l->s.write)
        tfs_cpu_relax();
}

static inline void tfs_rwticket_wrunlock(tfs_rwticket *l) {
    tfs_rwticket t = *l;

    tfs_barrier();

    t.s.write++;
    t.s.read++;

    *(unsigned short *)l = t.us;
}

static inline void tfs_rwticket_rdlock(tfs_rwticket *l) {
    unsigned int me = tfs_atomic_xadd(&l->u, (1 << 16));
    unsigned char val = me >> 16;

    while (val != l->s.read) {
        tfs_cpu_relax();
    }

    l->s.read++;
}

static inline void tfs_rwticket_rdunlock(tfs_rwticket *l) {
    tfs_atomic_inc(&l->s.write);
}

#endif
