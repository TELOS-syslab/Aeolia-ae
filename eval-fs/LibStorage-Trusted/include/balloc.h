#ifndef TFS_LIBFS_BALLOC_H_
#define TFS_LIBFS_BALLOC_H_

#include <pthread.h>
#include <stdatomic.h>

#include "./libfs-headers.h"
#include "./super.h"
#include "rbtree.h"

// just need use 1 page to store the bitmap
#define TFS_BLOCK_BITMAP_CHUNK 8

struct tfs_free_bm_block {
    int size;
    unsigned long st_block;
    unsigned long bm_lba;
    struct dma_buffer *dma_buffer;
    struct tfs_free_bm_block *next;
    struct tfs_free_bm_block *prev;
};

struct tfs_free_bm_block_list {
    struct tfs_free_bm_block *head;
    struct tfs_free_bm_block *tail;
};

struct tfs_free_list {
    pthread_spinlock_t s_lock;

    struct rb_root block_free_tree;
    struct tfs_range_node *first_node; // lowest address free range
    struct tfs_range_node *last_node;  // highest address free range

    struct tfs_free_bm_block_list *free_bm_blocks;

    unsigned long block_start;
    unsigned long block_end;

    unsigned long num_free_blocks;

    unsigned long num_blocknode;
};

extern atomic_uchar *tfs_block_own_map;

static inline unsigned long tfs_is_block_owned(unsigned long block) {
    return atomic_load(&tfs_block_own_map[block]) != 0;
}

static inline int tfs_block_clear_owned(unsigned long block) {
    return atomic_exchange(&tfs_block_own_map[block], 0);
}
static inline void tfs_block_set_owned(unsigned long block, int cpu) {
    atomic_store(&tfs_block_own_map[block], cpu + 1);
}

static inline int tfs_block_get_cpu(unsigned long block) {
    return atomic_load(&tfs_block_own_map[block]) - 1;
}

void tfs_alloc_block_free_lists(struct tfs_super_block *sb);

void tfs_delete_block_free_lists(struct tfs_super_block *sb);

void tfs_init_block_free_list(struct tfs_super_block *sb, int recovery);

int tfs_free_blocks(struct tfs_super_block *sb, unsigned long blocknr,
                    unsigned long num_blocks);

int tfs_new_blocks(struct tfs_super_block *sb, int num_blocks,
                   unsigned long *blocknr);

unsigned long tfs_count_free_blocks(struct tfs_super_block *sb);

#endif
