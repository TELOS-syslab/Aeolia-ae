#ifndef TFS_TRUSTED_STATES_H_
#define TFS_TRUSTED_STATES_H_

#include <linux/types.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "libfs-headers.h"
#include "super.h"
#include "util.h"

struct tfs_state {
    // protected
    unsigned long *authority_table_list[32];

    unsigned long authority_table_bytes;
    unsigned long authority_table_per_list_bytes;

    pthread_spinlock_t authority_table_lock;

    struct tfs_super_block *super_block;

    int pkey;
    uint32_t in_trust;
    uint32_t out_trust;

    // TODO: padding
};

extern struct tfs_state *tfs_state;

static inline unsigned long tfs_inode_to_sinode_lba(int ino) {
    return tfs_state->super_block->sinode_start + ((unsigned long)ino * FS_BLOCK_SIZE);
}

static inline void TFS_SET_R(unsigned long o, struct tfs_state *t) {
    unsigned long index = (o * 3UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset = (o * 3UL) % (t->authority_table_per_list_bytes * 8);
    set_bit(offset, t->authority_table_list[index]);
}

static inline void TFS_SET_W(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 1UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 1UL) % (t->authority_table_per_list_bytes * 8);
    set_bit(offset, t->authority_table_list[index]);
}

static inline void TFS_SET_JW(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 2UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 2UL) % (t->authority_table_per_list_bytes * 8);
    set_bit(offset, t->authority_table_list[index]);
}

static inline void TFS_CLR_R(unsigned long o, struct tfs_state *t) {
    unsigned long index = (o * 3UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset = (o * 3UL) % (t->authority_table_per_list_bytes * 8);
    clear_bit(offset, t->authority_table_list[index]);
}

static inline void TFS_CLR_W(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 1UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 1UL) % (t->authority_table_per_list_bytes * 8);
    clear_bit(offset, t->authority_table_list[index]);
}

static inline void TFS_CLR_JW(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 2UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 2UL) % (t->authority_table_per_list_bytes * 8);
    clear_bit(offset, t->authority_table_list[index]);
}

static inline unsigned long TFS_TEST_R(unsigned long o, struct tfs_state *t) {
    unsigned long index = (o * 3UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset = (o * 3UL) % (t->authority_table_per_list_bytes * 8);
    return test_bit(offset, t->authority_table_list[index]);
}

static inline unsigned long TFS_TEST_W(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 1UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 1UL) % (t->authority_table_per_list_bytes * 8);
    return test_bit(offset, t->authority_table_list[index]);
}

static inline unsigned long TFS_TEST_JW(unsigned long o, struct tfs_state *t) {
    unsigned long index =
        ((o * 3UL) + 2UL) / (t->authority_table_per_list_bytes * 8);
    unsigned long offset =
        ((o * 3UL) + 2UL) % (t->authority_table_per_list_bytes * 8);
    return test_bit(offset, t->authority_table_list[index]);
}

#endif // TFS_TRUSTED_STATES_H_
