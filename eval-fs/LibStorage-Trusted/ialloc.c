#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "./include/cmd.h"
#include "./include/ialloc.h"
#include "./include/journal.h"
#include "./include/logger.h"
#include "./include/tls.h"
#include "./include/trusted-states.h"

/* a bit map indicates whether the inode is allocated or not
 * set: allocated
 *
 * init: clear
 * allocate: set
 * free or unmap: clear
 */
atomic_uchar *tfs_inode_alloc_map = NULL;

static inline int tfs_ibm_test_bit(char *bm, int inode_offset) {
    return bm[inode_offset / 8] & (1 << (inode_offset % 8));
}

static inline void tfs_ibm_clear_bit(char *bm, int inode_offset) {
    bm[inode_offset / 8] &= ~(1 << (inode_offset % 8));
}

static inline void tfs_ibm_set_bit(char *bm, int inode_offset) {
    bm[inode_offset / 8] |= (1 << (inode_offset % 8));
}

static void ibm_io_callback(void *data) { 
    (*((volatile int *)data))++; 
}

// OK
void tfs_link_inode_bm_block(struct tfs_inode_bm_block_list *list,
                             struct tfs_inode_bm_block *block) {
    struct tfs_inode_bm_block **head = &list->head;
    struct tfs_inode_bm_block **tail = &list->tail;

    block->next = *head;
    if (*head)
        (*head)->prev = block;
    *head = block;
    if (!*tail)
        *tail = block;
}

// OK
void tfs_unlink_inode_bm_block(struct tfs_inode_bm_block_list *list,
                               struct tfs_inode_bm_block *block) {
    struct tfs_inode_bm_block **head = &list->head;
    struct tfs_inode_bm_block **tail = &list->tail;

    if (block->prev)
        block->prev->next = block->next;
    if (block->next)
        block->next->prev = block->prev;

    if (*head == block)
        *head = block->next;
    if (*tail == block)
        *tail = block->prev;
}

// OK
int tfs_inode_bm_block_fini(struct tfs_inode_bm_block *block) {
    int ret = 0;

    if (block->dma_buffer) {
        // ret = delete_dma_buffer(tfs_tls_ls_nvme_dev(), block->dma_buffer);
    }

    return ret;
}

// OK
static void tfs_add_inode_free_lists(struct tfs_inode_free_list *free_list,
                                     struct tfs_inode_bm_block *bm_block) {
    int i = 0;
    int tot = 0;

    for (i = 0; i < bm_block->size; i++)
        if (!tfs_ibm_test_bit((char *)(bm_block->dma_buffer->buf), i)) {
            tot++;
            struct tfs_inode_free_item *item =
                malloc(sizeof(struct tfs_inode_free_item));

            item->ino = bm_block->st_inode + i;
            item->bm_block = bm_block;
            if (free_list->free_inode_head != NULL) {
                free_list->free_inode_head->prev = item;
            }
            item->next = free_list->free_inode_head;
            item->prev = NULL;
            free_list->free_inode_head = item;
        }
}

// OK
static void __tfs_alloc_inode_from_kernel(struct tfs_inode_free_list *free_list,
                                          int cpu) {
    int ret = 0;
    int num_io = 0;
    struct tfs_inode_bm_block *block;
    int bm_num = TFS_INODE_BITMAP_CHUNK;
    ls_nvme_qp *qp = tfs_tls_ls_nvme_qp();

    block =
        (struct tfs_inode_bm_block *)malloc(sizeof(struct tfs_inode_bm_block));
    block->dma_buffer = (struct dma_buffer *)malloc(sizeof(struct dma_buffer));
    memset(block->dma_buffer, 0, sizeof(struct dma_buffer));
    create_dma_buffer(tfs_tls_ls_nvme_dev(), block->dma_buffer, SUFS_PAGE_SIZE);
    if (block->dma_buffer->buf == NULL) {
        LOG_ERROR("Failed to allocate DMA buffer\n");
        return;
    }
    block->next = NULL;
    block->prev = NULL;
    block->size = SUFS_PAGE_SIZE * 8;

    ret =
        tfs_cmd_alloc_inodes(&(block->lba), &(block->st_inode), (&bm_num), cpu);

    if (ret < 0 || bm_num != TFS_INODE_BITMAP_CHUNK) {
        LOG_ERROR("alloc inode: num_blk %d cpu %d failed\n", bm_num, cpu);
        return;
    }

    ret = read_blk_async(qp, block->lba, SUFS_PAGE_SIZE,
                         block->dma_buffer, ibm_io_callback, &num_io);

    if (ret < 0) {
        LOG_ERROR("read inode bm block failed\n");
        return;
    }

    
    while (*((volatile int*)(&num_io)) == 0) {
        block_idle(qp);
        // wait for the read to finish
    }

    tfs_link_inode_bm_block(free_list->inode_bm_blocks, block);
    tfs_add_inode_free_lists(free_list, block);
}

// OK
void tfs_inode_bm_buffer_set(struct tfs_inode_bm_block *bm_block, int inode) {
    if (inode < bm_block->st_inode ||
        inode >= bm_block->st_inode + bm_block->size) {
        LOG_ERROR("inode %d is out of range\n", inode);
        return;
    }
    tfs_run_journal(tfs_state->super_block->journal, bm_block->lba,
                    SUFS_PAGE_SIZE, bm_block->dma_buffer);

    tfs_ibm_set_bit(bm_block->dma_buffer->buf, inode - bm_block->st_inode);

}

// OK
void tfs_inode_bm_buffer_clear(struct tfs_inode_bm_block *bm_block, int inode) {
    if (inode < bm_block->st_inode ||
        inode >= bm_block->st_inode + bm_block->size) {
        LOG_ERROR("inode %d is out of range\n", inode);
        return;
    }
    tfs_run_journal(tfs_state->super_block->journal, bm_block->lba,
                    SUFS_PAGE_SIZE, bm_block->dma_buffer);
    tfs_ibm_clear_bit(bm_block->dma_buffer->buf, inode - bm_block->st_inode);
}

// OK
static void tfs_init_ialloc_map(void) {
    tfs_inode_alloc_map = calloc(1, SUFS_MAX_INODE_NUM);

    if (!tfs_inode_alloc_map) {
        fprintf(stderr, "Cannot allocate inode map!\n");
        abort();
    }
}

/* init */
void tfs_alloc_inode_free_lists(struct tfs_super_block *sb) {
    struct tfs_inode_free_list *ilist = NULL;
    int i = 0;

    sb->inode_free_lists =
        calloc(SUFS_MAX_CPU, sizeof(struct tfs_inode_free_list));

    if (!sb->inode_free_lists) {
        fprintf(stderr, "%s: Allocating inode maps failed.", __func__);
        abort();
    }

    tfs_init_ialloc_map();

    for (i = 0; i < SUFS_MAX_CPU; i++) {
        ilist = &(sb->inode_free_lists[i]);
        ilist->free_inode_head = NULL;
        ilist->inode_bm_blocks = malloc(sizeof(struct tfs_inode_bm_block_list));
        ilist->inode_bm_blocks->head = NULL;
        ilist->inode_bm_blocks->tail = NULL;
        pthread_spin_init(&ilist->lock, PTHREAD_PROCESS_SHARED);
    }
}

// OK
static void tfs_free_one_inode_free_list(struct tfs_inode_free_list *ilist) {
    struct tfs_inode_free_item *iter_inode = ilist->free_inode_head;
    struct tfs_inode_free_item *prev_inode = NULL;

    struct tfs_inode_bm_block *iter_bm = ilist->inode_bm_blocks->head;
    struct tfs_inode_bm_block *prev_bm = NULL;

    while (iter_inode != NULL) {
        prev_inode = iter_inode;
        iter_inode = iter_inode->next;
        free(prev_inode);
    }

    while (iter_bm != NULL) {
        tfs_cmd_free_inodes(iter_bm->lba, TFS_INODE_BITMAP_CHUNK);

        prev_bm = iter_bm;
        iter_bm = iter_bm->next;
        free(prev_bm);
    }
    free(ilist->inode_bm_blocks);
}

// OK
void tfs_free_inode_free_lists(struct tfs_super_block *sb) {
    struct tfs_inode_free_list *ilist = NULL;
    int i = 0;

    for (i = 0; i < SUFS_MAX_CPU; i++) {
        ilist = &(sb->inode_free_lists[i]);

        if (ilist) {
            tfs_free_one_inode_free_list(ilist);
        }
    }

    if (sb->inode_free_lists)
        free(sb->inode_free_lists);

    sb->inode_free_lists = NULL;
}

// OK
int tfs_init_inode_free_lists(struct tfs_super_block *sb) {
    int i = 0, cpus = 0;

    struct tfs_inode_free_list *ilist = NULL;

    cpus = SUFS_MAX_CPU;

    for (i = 0; i < cpus; i++) {
        ilist = &(sb->inode_free_lists[i]);
        __tfs_alloc_inode_from_kernel(ilist, i);
    }

    return 0;
}

// done
int tfs_free_inode(struct tfs_super_block *sb, int ino) {
    struct tfs_inode_free_list *free_list = NULL;
    int cpu = 0;
    struct tfs_inode_free_item *item = NULL;
    struct tfs_inode_bm_block *bm_block = NULL;
    cpu = tfs_inode_clear_allocated(ino);
    if (cpu < 0 || cpu >= SUFS_MAX_CPU) {
        // let it go
        LOG_INFO("inode %d is not on this cpu.. let it go..\n", ino);
        return 0;
    }

    free_list = &(sb->inode_free_lists[cpu]);

    pthread_spin_lock(&free_list->lock);
    bm_block = free_list->inode_bm_blocks->head;
    while (bm_block != NULL) {
        if (ino >= bm_block->st_inode &&
            ino < bm_block->st_inode + bm_block->size) {
            break;
        }
        bm_block = bm_block->next;
    }

    if (bm_block == NULL) {
        LOG_ERROR("inode %d is not in any bm block\n", ino);
        pthread_spin_unlock(&free_list->lock);
        return -ENOENT;
    }

    tfs_inode_bm_buffer_clear(bm_block, ino);

    pthread_spin_unlock(&free_list->lock);

    return 0;
}

// done
int tfs_new_inode(struct tfs_super_block *sb, int cpu) {
    struct tfs_inode_free_list *free_list = NULL;
    struct tfs_inode_free_item *item = NULL;
    int ret = 0;

    free_list = &(sb->inode_free_lists[cpu]);
    pthread_spin_lock(&free_list->lock);

    /* empty */
    if (free_list->free_inode_head == NULL) {
        __tfs_alloc_inode_from_kernel(free_list, cpu);

        if (free_list->free_inode_head == NULL) {
            pthread_spin_unlock(&free_list->lock);
            return 0;
        }
    }

    ret = free_list->free_inode_head->ino;
    tfs_inode_bm_buffer_set(free_list->free_inode_head->bm_block, ret);

    item = free_list->free_inode_head;
    free_list->free_inode_head = item->next;
    if(free_list->free_inode_head != NULL) {
        free_list->free_inode_head->prev = NULL;
    }

    free(item);

    if (item->next != NULL) {
        item->next->prev = item;
    }

    pthread_spin_unlock(&free_list->lock);

    tfs_inode_set_allocated(ret, cpu);

    return ret;
}
