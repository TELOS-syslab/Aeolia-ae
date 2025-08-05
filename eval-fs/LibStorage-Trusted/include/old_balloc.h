#ifndef TFS_BALLOC_H_
#define TFS_BALLOC_H_

#include <pthread.h>

struct tfs_block_bm_block {
    int in_use;
    unsigned long st_block;
    unsigned long lba;
    struct dma_buffer *dma_buffer;
    struct tfs_block_bm_block *next;
    struct tfs_block_bm_block *prev;
};

struct __attribute__((aligned(64))) tfs_block_bm_block_list {
    union {
        struct {
            struct tfs_block_bm_block *head;
            struct tfs_block_bm_block *tail;
            pthread_spinlock_t lock;
        };
        char padding[64];
    };
};

struct tfs_block_bm_block *tfs_block_bm_block_init(unsigned long lba);

void tfs_link_block_bm_block(struct tfs_block_bm_block_list *list,
                             struct tfs_block_bm_block *block);

void tfs_unlink_block_bm_block(struct tfs_block_bm_block_list *list,
                               struct tfs_block_bm_block *block);

int tfs_block_bm_block_fini(struct tfs_block_bm_block *block);

int tfs_read_block_bm_block(struct tfs_block_bm_block *bm_block);

void tfs_block_bm_block_list_init(struct tfs_block_bm_block_list *list,
                                  int cpu);

void tfs_block_bm_block_alloc(struct tfs_state *tfs_sb, unsigned long *bbm_lba,
                              unsigned long *block, char *bm_buffer, int cpu);

void tfs_block_bm_block_free(struct tfs_state *tfs_sb, unsigned long lba);

#endif // TFS_BALLOC_H_
