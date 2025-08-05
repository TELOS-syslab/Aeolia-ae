#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "./include/cmd.h"
#include "./include/ialloc.h"
#include "./include/journal.h"
#include "./include/logger.h"
#include "./include/rb.h"
#include "./include/tls.h"
#include "./include/trusted-states.h"
#include "./include/util.h"

/* a bit map indicates whether the block is owned or not
 * set: owned
 * See the comments on ialloc.c to see how this map should be maintained
 */

atomic_uchar *tfs_block_own_map = NULL;

static void bbm_io_callback(void *data) { (*((volatile int *)data))++; }

static inline int tfs_bbm_test_bit(char *bm, int block_offset) {
    return bm[block_offset / 8] & (1 << (block_offset % 8));
}

static inline void tfs_bbm_clear_bit(char *bm, int block_offset) {
    bm[block_offset / 8] &= ~(1 << (block_offset % 8));
}

static inline void tfs_bbm_set_bit(char *bm, int block_offset) {
    bm[block_offset / 8] |= (1 << (block_offset % 8));
}

static inline struct tfs_free_list *
tfs_get_free_list(struct tfs_super_block *sb, int cpu) {
    return &sb->block_free_lists[cpu];
}

// OK
void tfs_link_free_bm_block(struct tfs_free_bm_block_list *list,
                            struct tfs_free_bm_block *block) {
    struct tfs_free_bm_block **head = &list->head;
    struct tfs_free_bm_block **tail = &list->tail;

    block->next = *head;
    if (*head)
        (*head)->prev = block;
    *head = block;
    if (!*tail)
        *tail = block;
}

// OK
void tfs_unlink_free_bm_block(struct tfs_free_bm_block_list *list,
                              struct tfs_free_bm_block *block) {
    struct tfs_free_bm_block **head = &list->head;
    struct tfs_free_bm_block **tail = &list->tail;

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
int tfs_free_bm_block_fini(struct tfs_free_bm_block *block) {
    int ret = 0;

    if (block->dma_buffer) {
        // ret = delete_dma_buffer(tfs_tls_ls_nvme_dev(), block->dma_buffer);
    }

    return ret;
}

// OK
void tfs_init_balloc_map(struct tfs_super_block *sb) {
    long tot_block = FS_FREE_PAGES;
    LOG_INFO("total blocks: %ld\n", tot_block);

    tfs_block_own_map = calloc(1, tot_block);

    if (!tfs_block_own_map) {
        fprintf(stderr, "Cannot allocate block map!\n");
        abort();
    }
}

// OK
static int tfs_insert_blocktree(struct rb_root *tree,
                                struct tfs_range_node *new_node) {
    int ret = 0;

    ret = tfs_rbtree_insert_range_node(tree, new_node, NODE_BLOCK);
    if (ret)
        LOG_ERROR("ERROR: %s failed %d\n", __func__, ret);

    return ret;
}

/* init */
// OK
void tfs_alloc_block_free_lists(struct tfs_super_block *sb) {
    struct tfs_free_list *free_list = NULL;
    int i = 0;

    int cpus = SUFS_MAX_CPU;

    sb->block_free_lists = calloc(cpus, sizeof(struct tfs_free_list));

    if (!sb->block_free_lists) {
        fprintf(stderr, "Cannot allocate free_lists!\n");
        abort();
    }

    tfs_init_balloc_map(sb);

    for (i = 0; i < cpus; i++) {
        free_list = tfs_get_free_list(sb, i);
        free_list->block_free_tree = RB_ROOT;
        free_list->free_bm_blocks = malloc(sizeof(struct tfs_free_bm_block));
        free_list->free_bm_blocks->head = NULL;
        free_list->free_bm_blocks->tail = NULL;
        pthread_spin_init(&free_list->s_lock, PTHREAD_PROCESS_SHARED);
    }
}

// OK
static void tfs_free_one_block_free_list(struct tfs_free_list *free_list) {

    struct tfs_free_bm_block *bm_block = NULL;
    struct tfs_free_bm_block *prev = NULL;

    bm_block = free_list->free_bm_blocks->head;
    while (bm_block) {
        tfs_cmd_free_blocks(bm_block->bm_lba, bm_block->size);
        prev = bm_block;
        bm_block = bm_block->next;
        free(prev);
    }
}

// OK
void tfs_delete_block_free_lists(struct tfs_super_block *sb) {
    struct tfs_free_list *free_list = NULL;
    int i = 0;

    int cpus = SUFS_MAX_CPU;

    for (i = 0; i < cpus; i++) {
        free_list = tfs_get_free_list(sb, i);
        tfs_free_one_block_free_list(free_list);
    }

    /* Each tree is freed in save_blocknode_mappings */
    if (sb->block_free_lists)
        free(sb->block_free_lists);

    sb->block_free_lists = NULL;
}

/*
 * Initialize a free list.  Each CPU gets an equal share of the block space to
 * manage.
 */
// OK
static void tfs_init_free_list(struct tfs_super_block *sb,
                               struct tfs_free_list *free_list, int cpu) {

    int ret = 0;
    int num_io = 0;
    struct tfs_free_bm_block *bm_block;
    int num = TFS_BLOCK_BITMAP_CHUNK;
    ls_nvme_qp *qp = tfs_tls_ls_nvme_qp();

    bm_block = malloc(sizeof(struct tfs_free_bm_block));
    bm_block->dma_buffer =
        (struct dma_buffer *)malloc(sizeof(struct dma_buffer));
    memset(bm_block->dma_buffer, 0, sizeof(struct dma_buffer));
    create_dma_buffer(tfs_tls_ls_nvme_dev(), bm_block->dma_buffer,
                      SUFS_PAGE_SIZE);
    bm_block->next = NULL;
    bm_block->prev = NULL;
    bm_block->size = SUFS_PAGE_SIZE * 8;

    ret = tfs_cmd_alloc_blocks(&(bm_block->bm_lba), &(bm_block->st_block),
                               &(num), cpu);

    if (ret < 0 || num != TFS_BLOCK_BITMAP_CHUNK) {
        LOG_ERROR("alloc block: num_blk 1 cpu %d, num %d failed\n", cpu, num);
        return;
    }

    ret = read_blk_async(qp, bm_block->bm_lba, SUFS_PAGE_SIZE,
                         bm_block->dma_buffer, bbm_io_callback, &num_io);

    if (ret < 0) {
        LOG_ERROR("free_list: read block bm block failed\n");
        return;
    }

    while (*((volatile int*)(&num_io)) == 0) {
        block_idle(qp);
        // wait for the read to finish
    }

    tfs_link_free_bm_block(free_list->free_bm_blocks, bm_block);

    free_list->block_start = bm_block->st_block;
    free_list->block_end = bm_block->st_block + SUFS_PAGE_SIZE * 8 - 1;
}

// OK
void tfs_init_block_free_list(struct tfs_super_block *sb, int recovery) {
    struct rb_root *tree = NULL;
    struct tfs_range_node *blknode = NULL;
    struct tfs_free_list *free_list = NULL;
    int i = 0, j = 0, cpus = 0;
    int ret = 0;
    unsigned long block_start = 0, block_end = 0;

    cpus = SUFS_MAX_CPU;

    /* Divide the block range among per-CPU free lists */
    for (i = 0; i < cpus; i++) {
        free_list = tfs_get_free_list(sb, i);
        tree = &(free_list->block_free_tree);

        tfs_init_free_list(sb, free_list, i);

        /* For recovery, update these fields later */
        if (recovery == 0) {
            free_list->num_free_blocks = 0;

            block_start = free_list->block_start;
            block_end = block_start;
            free_list->num_blocknode = 0;

            for (j = 0; j < SUFS_PAGE_SIZE * 8; j++) {
                if (!tfs_bbm_test_bit(
                        free_list->free_bm_blocks->head->dma_buffer->buf, j)) {
                    free_list->num_free_blocks++;
                    block_end++;
                } else if (block_end > block_start) {
                    blknode = tfs_alloc_range_node();
                    if (blknode == NULL) {
                        fprintf(stderr, "Cannot alloc blknode!\n");
                        abort();
                    }
                    blknode->range_low = block_start;
                    blknode->range_high = block_end - 1;
                    ret = tfs_insert_blocktree(tree, blknode);
                    if (ret) {
                        LOG_ERROR("%s failed\n", __func__);
                        tfs_free_range_node(blknode);
                        return;
                    }
                    if (free_list->first_node == NULL) {
                        free_list->first_node = blknode;
                    }
                    free_list->last_node = blknode;
                    free_list->num_blocknode++;
                    block_end++;
                    block_start = block_end;
                } else {
                    block_start++;
                    block_end++;
                }
            }

            if (block_end > block_start) {
                blknode = tfs_alloc_range_node();
                if (blknode == NULL) {
                    fprintf(stderr, "Cannot alloc blknode!\n");
                    abort();
                }
                blknode->range_low = block_start;
                blknode->range_high = block_end - 1;
                ret = tfs_insert_blocktree(tree, blknode);
                if (ret) {
                    LOG_ERROR("%s failed\n", __func__);
                    tfs_free_range_node(blknode);
                    return;
                }
                if (free_list->first_node == NULL) {
                    free_list->first_node = blknode;
                }
                free_list->last_node = blknode;
                free_list->num_blocknode++;
            }
        }
    }
}

// OK
static void tfs_free_bm_buffer_set(struct tfs_free_bm_block *bm_block,
                                   unsigned long blocknr, int num) {
    unsigned long i;
    if (blocknr < bm_block->st_block ||
        blocknr >= bm_block->st_block + bm_block->size) {
        LOG_ERROR("block %lu is out of range\n", blocknr);
        return;
    }
    tfs_run_journal(tfs_state->super_block->journal, bm_block->bm_lba,
                    bm_block->dma_buffer->size, bm_block->dma_buffer);
    for (i = 0; i < num; i++) {
        tfs_bbm_set_bit(bm_block->dma_buffer->buf,
                        blocknr + i - bm_block->st_block);
    }
}

// OK
static void tfs_free_bm_buffer_clear(struct tfs_free_bm_block *bm_block,
                                     unsigned long blocknr, int num) {
    unsigned long i;
    if (blocknr < bm_block->st_block ||
        blocknr >= bm_block->st_block + bm_block->size) {
        LOG_ERROR("block %lu is out of range\n", blocknr);
        return;
    }
    tfs_run_journal(tfs_state->super_block->journal, bm_block->bm_lba,
                    bm_block->dma_buffer->size, bm_block->dma_buffer);
    for (i = 0; i < num; i++) {
        tfs_bbm_clear_bit(bm_block->dma_buffer->buf,
                          blocknr + i - bm_block->st_block);
    }

}

/* Used for both block free tree and inode inuse tree */
// OK
static int tfs_find_free_slot(struct rb_root *tree, unsigned long range_low,
                              unsigned long range_high,
                              struct tfs_range_node **prev,
                              struct tfs_range_node **next) {
    struct tfs_range_node *ret_node = NULL;
    struct rb_node *tmp = NULL;
    int ret = 0;

    ret = tfs_rbtree_find_range_node(tree, range_low, NODE_BLOCK, &ret_node);
    if (ret) {
        LOG_ERROR("%s ERROR: %lu - %lu already in free list\n", __func__,
                  range_low, range_high);
        return -EINVAL;
    }

    if (!ret_node) {
        *prev = *next = NULL;
    } else if (ret_node->range_high < range_low) {
        *prev = ret_node;
        tmp = rb_next(&ret_node->node);
        if (tmp) {
            *next = container_of(tmp, struct tfs_range_node, node);
        } else {
            *next = NULL;
        }
    } else if (ret_node->range_low > range_high) {
        *next = ret_node;
        tmp = rb_prev(&ret_node->node);
        if (tmp) {
            *prev = container_of(tmp, struct tfs_range_node, node);
        } else {
            *prev = NULL;
        }
    } else {
        LOG_ERROR("%s ERROR: %lu - %lu overlaps with existing "
                  "node %lu - %lu\n",
                  __func__, range_low, range_high, ret_node->range_low,
                  ret_node->range_high);
        return -EINVAL;
    }

    return 0;
}

int __tfs_free_blocks(struct tfs_free_list *free_list, unsigned long blocknr,
                      unsigned long num_blocks, int wb) {

    struct rb_root *tree = NULL;
    unsigned long block_low = 0;
    unsigned long block_high = 0;
    struct tfs_range_node *prev = NULL;
    struct tfs_range_node *next = NULL;
    struct tfs_range_node *curr_node = NULL;
    int new_node_used = 0;
    int ret = 0, i = 0;
    struct tfs_free_bm_block *bm_block = NULL;

    /* Pre-allocate blocknode */
    curr_node = tfs_alloc_range_node();
    if (curr_node == NULL) {
        /* returning without freeing the block*/
        return -ENOMEM;
    }

    tree = &(free_list->block_free_tree);

    block_low = blocknr;
    block_high = blocknr + num_blocks - 1;

    ret = tfs_find_free_slot(tree, block_low, block_high, &prev, &next);

    if (ret) {
        LOG_ERROR("%s: find free slot fail: %d\n", __func__, ret);
        goto out;
    }

    if (prev && next && (block_low == prev->range_high + 1) &&
        (block_high + 1 == next->range_low)) {
        /* fits the hole */
        rb_erase(&next->node, tree);
        free_list->num_blocknode--;
        prev->range_high = next->range_high;
        if (free_list->last_node == next)
            free_list->last_node = prev;
        tfs_free_range_node(next);
        goto block_found;
    }

    if (prev && (block_low == prev->range_high + 1)) {
        /* Aligns left */
        prev->range_high += num_blocks;
        goto block_found;
    }

    if (next && (block_high + 1 == next->range_low)) {
        /* Aligns right */
        next->range_low -= num_blocks;
        goto block_found;
    }

    /* Aligns somewhere in the middle */
    curr_node->range_low = block_low;
    curr_node->range_high = block_high;

    new_node_used = 1;
    ret = tfs_insert_blocktree(tree, curr_node);
    if (ret) {
        new_node_used = 0;
        goto out;
    }

    if (!prev)
        free_list->first_node = curr_node;
    if (!next)
        free_list->last_node = curr_node;

    free_list->num_blocknode++;

block_found:
    free_list->num_free_blocks += num_blocks;
    bm_block = free_list->free_bm_blocks->head;

    for (i = block_low; i <= block_high; i++) {
        tfs_block_clear_owned(i);
    }
    // to bad..

    int f = 0;
    while (bm_block != NULL) {
        // LOG_ERROR("%s: block %lu - %lu not in free bm block %lu - %lu\n",
        //             __func__, block_low, block_high,
        //             bm_block->st_block,
        //             bm_block->st_block + bm_block->size);
        if (!(block_low >= bm_block->st_block + bm_block->size ||
                block_high < bm_block->st_block)) {
            unsigned long st, ed;
            st = (bm_block->st_block >= block_low ?
                    bm_block->st_block : block_low);
            ed = (bm_block->st_block + bm_block->size - 1 <= block_high ?
                    bm_block->st_block + bm_block->size - 1 : block_high);
            if (wb)
                tfs_free_bm_buffer_clear(bm_block, st,
                    ed - st + 1);
            f = 1;
        }
        bm_block = bm_block->next;
    }
    
    if (f == 0) {
        LOG_ERROR("%s: block %lu - %lu not in free bm block\n", __func__,
                    block_low, block_high);
        return -EINVAL;
    }

out:

    if (new_node_used == 0)
        tfs_free_range_node(curr_node);

    return ret;
}

/*
 * This has the implicit assumption that the freed block chunk only belongs
 * to one CPU pool
 */
// OK
int tfs_free_blocks(struct tfs_super_block *sb, unsigned long blocknr,
                    unsigned long num_blocks) {
    struct tfs_free_list *free_list = NULL;
    int cpuid = 0;
    int ret = 0;

    if (num_blocks <= 0) {
        LOG_ERROR("%s ERROR: free %lu\n", __func__, num_blocks);
        return -EINVAL;
    }

    cpuid = tfs_block_get_cpu(blocknr);
    LOG_INFO("blocknr %lx is on cpu %d\n", blocknr, cpuid);
    if (cpuid < 0 || cpuid >= SUFS_MAX_CPU) {
        // TODO: let it go..
        LOG_INFO("blocknr %lx is not this cpu, let it go..\n", blocknr);
        return 0;
    }

    free_list = tfs_get_free_list(sb, cpuid);

    pthread_spin_lock(&free_list->s_lock);

    ret = __tfs_free_blocks(free_list, blocknr, num_blocks, 1);

    pthread_spin_unlock(&free_list->s_lock);

    return ret;
}

// OK
static int not_enough_blocks(struct tfs_free_list *free_list,
                             unsigned long num_blocks) {
    struct tfs_range_node *first = free_list->first_node;
    struct tfs_range_node *last = free_list->last_node;

    /*
     * free_list->num_free_blocks / free_list->num_blocknode is used to
     * handle fragmentation within blocknodes
     */
    if (!first || !last ||
        free_list->num_free_blocks / free_list->num_blocknode < num_blocks) {
#if 0
        printf("%s: num_free_blocks=%ld; num_blocks=%ld; "
                "first=0x%p; last=0x%p\n", __func__, free_list->num_free_blocks,
                num_blocks, first, last);
#endif
        return 1;
    }

    return 0;
}

/* Return how many blocks allocated */
// OK
static long tfs_alloc_blocks_in_free_list(struct tfs_free_list *free_list,
                                          unsigned long num_blocks,
                                          unsigned long *new_blocknr) {
    struct rb_root *tree = NULL;
    struct tfs_range_node *curr = NULL, *next = NULL, *prev = NULL;
    struct rb_node *temp = NULL, *next_node = NULL, *prev_node = NULL;
    unsigned long curr_blocks = 0;
    bool found = 0;

    unsigned long step = 0;

    if (!free_list->first_node || free_list->num_free_blocks == 0) {
        LOG_ERROR("%s: Can't alloc. free_list->first_node=0x%p "
                  "free_list->num_free_blocks = %lu\n",
                  __func__, free_list->first_node, free_list->num_free_blocks);
        return -ENOSPC;
    }

    tree = &(free_list->block_free_tree);
    temp = &(free_list->first_node->node);

    /* always use the unaligned approach */
    while (temp) {
        step++;
        curr = container_of(temp, struct tfs_range_node, node);

        curr_blocks = curr->range_high - curr->range_low + 1;
        LOG_INFO("numblock %lu, step %lu, curr_blocks %lu, high %lu, low %lu\n", num_blocks, step,
                 curr_blocks, curr->range_high, curr->range_low);

        if (num_blocks >= curr_blocks) {
            if (num_blocks > curr_blocks)
                goto next;

            /* Otherwise, allocate the whole blocknode */
            if (curr == free_list->first_node) {
                next_node = rb_next(temp);
                if (next_node) {
                    next = container_of(next_node, struct tfs_range_node, node);
                }

                free_list->first_node = next;
            }

            if (curr == free_list->last_node) {
                prev_node = rb_prev(temp);
                if (prev_node) {
                    prev = container_of(prev_node, struct tfs_range_node, node);
                }

                free_list->last_node = prev;
            }

            rb_erase(&curr->node, tree);
            free_list->num_blocknode--;
            num_blocks = curr_blocks;
            *new_blocknr = curr->range_low;
            tfs_free_range_node(curr);
            found = 1;
            break;
        }

        /* Allocate partial blocknode */

        *new_blocknr = curr->range_low;
        curr->range_low += num_blocks;

        found = 1;
        break;
    next:
        temp = rb_next(temp);
    }

    if (free_list->num_free_blocks < num_blocks) {
        LOG_ERROR("%s: free list has %lu free blocks, "
                  "but allocated %lu blocks?\n",
                  __func__, free_list->num_free_blocks, num_blocks);
        return -ENOSPC;
    }

    if (found == 1) {
        struct tfs_free_bm_block *bm_block = free_list->free_bm_blocks->head;
        int f = 0;
        while (bm_block != NULL) {
            // LOG_ERROR("%s: block %lu - %lu not in free bm block %lu - %lu\n",
            //           __func__, *new_blocknr, *new_blocknr + num_blocks,
            //           bm_block->st_block,
            //           bm_block->st_block + bm_block->size);
            if (!(*new_blocknr >= bm_block->st_block + bm_block->size ||
                  *new_blocknr + num_blocks - 1 < bm_block->st_block)) {
                unsigned long st, ed;
                st = (bm_block->st_block >= *new_blocknr ?
                        bm_block->st_block : *new_blocknr);
                ed = (bm_block->st_block + bm_block->size - 1 <=
                        *new_blocknr + num_blocks - 1 ?
                        bm_block->st_block + bm_block->size - 1 :
                        *new_blocknr + num_blocks - 1);
                    tfs_free_bm_buffer_set(bm_block, st, ed - st + 1);
                f = 1;
            }
            bm_block = bm_block->next;
        }
        
        if (f == 0) {
            LOG_ERROR("%s: block %lu - %lu not in free bm block\n", __func__,
                      *new_blocknr, *new_blocknr + num_blocks);
            return -EINVAL;
        }
        free_list->num_free_blocks -= num_blocks;
    } else {
        LOG_ERROR("%s: Can't alloc.  found = %d\n", __func__, found);
        return -ENOSPC;
    }

    return num_blocks;
}

// OK
int tfs_new_blocks(struct tfs_super_block *sb, int num_blocks,
                   unsigned long *blocknr) {
    struct tfs_free_list *free_list = NULL;
    unsigned long new_blocknr = 0;
    long ret_blocks = 0;
    int i = 0;
    int j;
    int cpu;
    ls_nvme_qp *qp = tfs_tls_ls_nvme_qp();
    if (num_blocks == 0) {
        LOG_ERROR("%s: num_blocks == 0\n", __func__);
        return -EINVAL;
    }
    cpu = get_core_id_userspace();
    free_list = tfs_get_free_list(sb, cpu);
    pthread_spin_lock(&free_list->s_lock);
    // LOG_WARN("new_blocks: %d, cpu :%d\n", num_blocks, cpu);

    if (not_enough_blocks(free_list, num_blocks)) {
        LOG_INFO("not enough blocks: cpu %d, free_blocks %ld, required %lu\n", cpu,
                 free_list->num_free_blocks, num_blocks);
        unsigned long num = SUFS_PAGE_SIZE * 8;
        int num_bm = TFS_BLOCK_BITMAP_CHUNK;

        if (num_blocks > num) {
            num_bm =
                (((num_blocks + 7) / 8 + SUFS_PAGE_SIZE - 1) / SUFS_PAGE_SIZE) *
                8;
        }
        num_bm += TFS_BLOCK_BITMAP_CHUNK;

        int ret = 0;
        int num_io = 0;
        struct tfs_free_bm_block *bm_block;

        bm_block = malloc(sizeof(struct tfs_free_bm_block));
        bm_block->dma_buffer =
            (struct dma_buffer *)malloc(sizeof(struct dma_buffer));
        memset(bm_block->dma_buffer, 0, sizeof(struct dma_buffer));
        create_dma_buffer(tfs_tls_ls_nvme_dev(), bm_block->dma_buffer,
                          num_bm / 8 * SUFS_PAGE_SIZE);
        bm_block->next = NULL;
        bm_block->prev = NULL;
        bm_block->size = num_bm * SUFS_PAGE_SIZE;

        if (tfs_cmd_alloc_blocks(&(bm_block->bm_lba), &(bm_block->st_block),
                                 &(num_bm), cpu) < 0) {
            LOG_ERROR("alloc block: num_blk %d cpu %d failed\n", num_bm, cpu);

            pthread_spin_unlock(&free_list->s_lock);

            return -ENOMEM;
        }
        LOG_INFO("alloc block: num_blk %d cpu %d, lba %lx, st_block %lu\n",
                 num_bm, cpu, bm_block->bm_lba, bm_block->st_block);
        LOG_INFO("size %lu, st_block %lu\n", bm_block->dma_buffer->size,
                 bm_block->st_block);
        ret = read_blk_async(qp, bm_block->bm_lba,
                             bm_block->dma_buffer->size, bm_block->dma_buffer,
                             bbm_io_callback, &num_io);

        if (ret < 0) {
            LOG_ERROR("free_list: read block bm block failed\n");
            return -ENOMEM;
        }

        while (*((volatile int*)(&num_io)) == 0) {
            block_idle(qp);
            // wait for the read to finish
        }

        unsigned long block_start = 0, block_end = 0;
        struct tfs_range_node *blknode = NULL;
        struct tfs_range_node *prev = NULL;
        struct tfs_range_node *next = NULL;
        block_start = bm_block->st_block;
        block_end = block_start;

        for (j = 0; j < bm_block->size; j++) {
            if (!tfs_bbm_test_bit(
                    bm_block->dma_buffer->buf, j)) {
                free_list->num_free_blocks++;
                block_end++;
            } else if (block_end > block_start) {
                unsigned long nn_blocknr = block_end - block_start;
                blknode = tfs_alloc_range_node();
                if (blknode == NULL) {
                    fprintf(stderr, "Cannot alloc blknode!\n");
                    abort();
                }
                blknode->range_low = block_start;
                blknode->range_high = block_end - 1;

                ret = tfs_find_free_slot(&(free_list->block_free_tree), blknode->range_low, blknode->range_high, &prev, &next);

                if (ret) {
                    LOG_ERROR("%s: find free slot fail: %d\n", __func__, ret);
                    abort();
                }
                int found = 0;

                if (prev && next && (blknode->range_low == prev->range_high + 1) &&
                    (blknode->range_high + 1 == next->range_low)) {
                    /* fits the hole */
                    rb_erase(&next->node, &(free_list->block_free_tree));
                    free_list->num_blocknode--;
                    prev->range_high = next->range_high;
                    if (free_list->last_node == next)
                        free_list->last_node = prev;
                    tfs_free_range_node(next);
                    found = 1;
                }
            
                if (prev && (blknode->range_low == prev->range_high + 1)) {
                    /* Aligns left */
                    prev->range_high += nn_blocknr;
                    found = 1;
                }
            
                if (next && (blknode->range_high + 1 == next->range_low)) {
                    /* Aligns right */
                    next->range_low -= nn_blocknr;
                    found = 1;
                }
                if (!found) {
                    ret = tfs_insert_blocktree(&(free_list->block_free_tree), blknode);
                    
                    if (ret) {
                        LOG_ERROR("%s failed\n", __func__);
                        tfs_free_range_node(blknode);
                        return -ENOMEM;
                    }
                    if (!prev)
                        free_list->first_node = blknode;
                    if (!next)
                        free_list->last_node = blknode;
                    
                    free_list->num_blocknode++;
                        
                } else {
                    tfs_free_range_node(blknode);
                }
                block_end++;
                block_start = block_end;
                
            } else {
                block_start++;
                block_end++;
            }
        }

        if (block_end > block_start) {
            int nn_blocknr = block_end - block_start;
            blknode = tfs_alloc_range_node();
            if (blknode == NULL) {
                fprintf(stderr, "Cannot alloc blknode!\n");
                abort();
            }
            blknode->range_low = block_start;
            blknode->range_high = block_end - 1;
            ret = tfs_find_free_slot(&(free_list->block_free_tree), blknode->range_low, blknode->range_high, &prev, &next);

            if (ret) {
                LOG_ERROR("%s: find free slot fail: %d\n", __func__, ret);
                abort();
            }
            int found = 0;

            if (prev && next && (blknode->range_low == prev->range_high + 1) &&
                (blknode->range_high + 1 == next->range_low)) {
                /* fits the hole */
                rb_erase(&next->node, &(free_list->block_free_tree));
                free_list->num_blocknode--;
                prev->range_high = next->range_high;
                if (free_list->last_node == next)
                    free_list->last_node = prev;
                tfs_free_range_node(next);
                found = 1;
            }
        
            if (prev && (blknode->range_low == prev->range_high + 1)) {
                /* Aligns left */
                prev->range_high += nn_blocknr;
                found = 1;
            }
        
            if (next && (blknode->range_high + 1 == next->range_low)) {
                /* Aligns right */
                next->range_low -= nn_blocknr;
                found = 1;
            }
            if (!found) {
                ret = tfs_insert_blocktree(&(free_list->block_free_tree), blknode);
                
                if (ret) {
                    LOG_ERROR("%s failed\n", __func__);
                    tfs_free_range_node(blknode);
                    return -ENOMEM;
                }
                if (!prev)
                    free_list->first_node = blknode;
                if (!next)
                    free_list->last_node = blknode;
                
                free_list->num_blocknode++;
                    
            } else {
                tfs_free_range_node(blknode);
            }
        }

        tfs_link_free_bm_block(free_list->free_bm_blocks, bm_block);
    }

    ret_blocks =
        tfs_alloc_blocks_in_free_list(free_list, num_blocks, &new_blocknr);

        pthread_spin_unlock(&free_list->s_lock);

    if (ret_blocks <= 0 || new_blocknr == 0) {
        LOG_ERROR("%s: not able to allocate %lu blocks. "
                  "ret_blocks=%ld; new_blocknr=%lu\n",
                  __func__, num_blocks, ret_blocks, new_blocknr);
        return -ENOSPC;
    }

    for (i = new_blocknr; i < new_blocknr + ret_blocks; i++) {
        tfs_block_set_owned(i, cpu);
    }

    if (blocknr)
        *blocknr = new_blocknr;
        
    LOG_INFO("tfs_new_blocks: success cpu %d, blocknr %lu, num_blocks %lu\n", cpu,
             new_blocknr, num_blocks);
    return ret_blocks;
}

unsigned long tfs_count_free_blocks(struct tfs_super_block *sb) {
    struct tfs_free_list *free_list = NULL;
    unsigned long num_free_blocks = 0;
    int i = 0, cpus = 0;

    cpus = SUFS_MAX_CPU;

    for (i = 0; i < cpus; i++) {
        free_list = tfs_get_free_list(sb, i);
        num_free_blocks += free_list->num_free_blocks;
    }

    return num_free_blocks;
}
