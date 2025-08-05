#ifndef TFS_LIBFS_IALLOC_H_
#define TFS_LIBFS_IALLOC_H_

#include <pthread.h>

#include "./libfs-headers.h"
#include "balloc.h"
#include "super.h"

// just need use 1 page to store the bitmap
#define TFS_INODE_BITMAP_CHUNK 8

struct tfs_inode_bm_block {
    int st_inode;
    int size;
    unsigned long lba;
    struct dma_buffer *dma_buffer;
    struct tfs_inode_bm_block *next;
    struct tfs_inode_bm_block *prev;
};

struct tfs_inode_free_item {
    int ino;
    struct tfs_inode_bm_block *bm_block;
    struct tfs_inode_free_item *next;
    struct tfs_inode_free_item *prev;
};

struct tfs_inode_bm_block_list {
    struct tfs_inode_bm_block *head;
    struct tfs_inode_bm_block *tail;
};

struct tfs_inode_free_list {
    struct tfs_inode_free_item *free_inode_head;
    struct tfs_inode_bm_block_list *inode_bm_blocks;
    pthread_spinlock_t lock;
};

extern atomic_uchar *tfs_inode_alloc_map;

static inline int tfs_is_inode_allocated(int inode) {
    return atomic_load(&tfs_inode_alloc_map[inode]) != 0;
}

static inline int tfs_inode_clear_allocated(int inode) {
    return atomic_exchange(&tfs_inode_alloc_map[inode], 0) - 1;
}
static inline void tfs_inode_set_allocated(int inode, unsigned char cpu) {
    atomic_store(&tfs_inode_alloc_map[inode], cpu + 1);
}

static inline int tfs_inode_get_cpu(int inode) {
    return atomic_load(&tfs_inode_alloc_map[inode]) - 1;
}

void tfs_alloc_inode_free_lists(struct tfs_super_block *sb);

void tfs_free_inode_free_lists(struct tfs_super_block *sb);

int tfs_init_inode_free_lists(struct tfs_super_block *sb);

int tfs_free_inode(struct tfs_super_block *sb, int ino);

int tfs_new_inode(struct tfs_super_block *sb, int cpu);

struct tfs_inode_bm_block *tfs_inode_bm_block_init(unsigned long lba,
                                                   int inode);

void tfs_link_inode_bm_block(struct tfs_inode_bm_block_list *list,
                             struct tfs_inode_bm_block *block);

void tfs_unlink_inode_bm_block(struct tfs_inode_bm_block_list *list,
                               struct tfs_inode_bm_block *block);

int tfs_inode_bm_block_fini(struct tfs_inode_bm_block *block);

int tfs_read_inode_bm_block(struct tfs_inode_bm_block *bm_block);

#endif
