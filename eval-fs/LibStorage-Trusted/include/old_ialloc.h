#ifndef TFS_IALLOC_H_
#define TFS_IALLOC_H_

#include <pthread.h>

#include "trusted-states.h"

struct tfs_inode_bm_block {
    int in_use;
    int st_inode;
    unsigned long lba;
    struct dma_buffer *dma_buffer;
    struct tfs_inode_bm_block *next;
    struct tfs_inode_bm_block *prev;
};

struct __attribute__((aligned(64))) tfs_inode_bm_block_list {
    union {
        struct {
            struct tfs_inode_bm_block *head;
            struct tfs_inode_bm_block *tail;
            pthread_spinlock_t lock;
        };
        char padding[64];
    };
};

struct tfs_inode_bm_block *tfs_inode_bm_block_init(unsigned long lba,
                                                   int inode);

void tfs_link_inode_bm_block(struct tfs_inode_bm_block_list *list,
                             struct tfs_inode_bm_block *block);

void tfs_unlink_inode_bm_block(struct tfs_inode_bm_block_list *list,
                               struct tfs_inode_bm_block *block);

int tfs_inode_bm_block_fini(struct tfs_inode_bm_block *block);

int tfs_read_inode_bm_block(struct tfs_inode_bm_block *bm_block);

void tfs_inode_bm_block_list_init(struct tfs_inode_bm_block_list *list,
                                  int cpu);

void tfs_inode_bm_block_alloc(struct tfs_state *tfs_sb, unsigned long *bm_lba,
                              int *inode, char* bm_buffer, int cpu);

void tfs_inode_bm_block_free(struct tfs_state *tfs_sb, unsigned long lba);

#endif // TFS_IALLOC_H_
