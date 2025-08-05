#ifndef TFS_LIBFS_BRAVO_H_
#define TFS_LIBFS_BRAVO_H_

#include "libfs-headers.h"
#include <pthread.h>

#define TFS_RL_NUM_SLOT (1048573)
#define TFS_RL_TABLE_SIZE (1024 * 1024)

#define TFS_BRAVO_N 9

static inline unsigned long tfs_bravo_hash_int(unsigned long x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ul;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebul;
    x = x ^ (x >> 31);
    return x;
}

static inline unsigned int tfs_bravo_hash(unsigned long addr) {
    return tfs_bravo_hash_int(((unsigned long)pthread_self()) + addr) %
           TFS_RL_NUM_SLOT;
}

extern volatile unsigned long **tfs_global_vr_table;

void tfs_init_global_rglock_bravo(void);

void tfs_free_global_rglock_bravo(void);

#endif
