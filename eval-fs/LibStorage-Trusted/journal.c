#include "./include/journal.h"
#include "./include/bravo.h"
#include "./include/logger.h"
#include "./include/rwlock_bravo.h"
#include "./include/tls.h"
#include "./include/util.h"
#include <limits.h>

static void journal_async_callback(void *data) { (*((volatile int *)data))++; }

#define J_HASH_TO_HASH(x) ((struct tfs_chainhash *)(&(x)))

void tfs_init_journal(struct tfs_journal *journal) {
    int i;
    unsigned long bytes_per_block;
    tfs_bravo_rwlock_init(&(journal->rw_lock));
    journal->jhs = calloc(SUFS_MAX_CPU, sizeof(struct tfs_journal_header));
    if (!journal->jhs) {
        LOG_INFO("Failed to allocate journal header\n");
        abort();
    }
    bytes_per_block = FS_JOURNAL_BYTES / SUFS_MAX_CPU;
    for (i = 0; i < SUFS_MAX_CPU; i++) {
        journal->jhs[i].journal_tail = 0;
        journal->jhs[i].journal_lba_head =
            FS_TOTAL_BYTES + (unsigned long)i * bytes_per_block;
        journal->jhs[i].head = NULL;
        tfs_chainhash_init(J_HASH_TO_HASH(journal->map_[i]), 0);
    }

}

void tfs_run_journal(struct tfs_journal *journal, unsigned long lba, int size,
                     dma_buffer *buf) {
    int cpu = get_core_id_userspace();
    if(tfs_chainhash_lookup(J_HASH_TO_HASH(journal->map_[cpu]), (unsigned long)buf, 0, 0)){
        return;
    }

    tfs_chainhash_insert(J_HASH_TO_HASH(journal->map_[cpu]), (unsigned long)buf, 0, 0, NULL);
    
    struct tfs_journal_item *item = malloc(sizeof(struct tfs_journal_item));
    if (!item) {
        LOG_INFO("Failed to allocate journal item\n");
        abort();
    }
    item->block_header = malloc(sizeof(struct tfs_block_header));
    if (!item->block_header) {
        LOG_INFO("Failed to allocate block header\n");
        abort();
    }
    item->block_header->size = size;
    item->block_header->lba = lba;
    item->block_header->buf = buf;
    LOG_INFO("Run journal %p, lba %lx, size %d\n", item, lba, size);

    struct tfs_journal_header *jh = &(journal->jhs[cpu]);
    item->nxt = jh->head;
    jh->head = item;

}

void tfs_begin_journal(struct tfs_journal *journal) {
    tfs_bravo_read_lock(&(journal->rw_lock));
}

void tfs_exit_journal(struct tfs_journal *journal) {
    tfs_bravo_read_unlock(&(journal->rw_lock));
}

void tfs_commit_and_checkpoint_journal(struct tfs_journal *journal) {
    int i;
    unsigned long jlba;
    int tot_j = 0;
    int num_j = 0;
    ls_nvme_qp *qp = tfs_tls_ls_nvme_qp();

    LOG_INFO("Starting to write journal items\n");
    tfs_bravo_write_lock(&(journal->rw_lock));
    LOG_INFO("Writing journal items\n");
    num_j = 0;
    for (i = 0; i < SUFS_MAX_CPU; i++) {
        struct tfs_journal_header *jh = &(journal->jhs[i]);
        struct tfs_journal_item *item = jh->head;
        jlba = jh->journal_lba_head;
        while (item) {
            struct tfs_journal_item *next = item->nxt;
            // todo work around
            if((unsigned long)item->block_header->buf->buf < 0x100000ul || (unsigned long)item->block_header->buf->pa < 0x100000ul){
                item = next;
                continue;
            }
            tot_j++;
            LOG_INFO("Writing journal item %p, lba %lx, size %d\n", item, jlba,
                     item->block_header->size);
            
            write_blk_async(qp, jlba,
                            item->block_header->size, item->block_header->buf,
                            journal_async_callback, &num_j);
            if (tot_j >= 64) {
                while (*((volatile int*)(&num_j)) < tot_j) {
                    block_idle(qp);
                    // wait for all journal writes to finish
                }
                *((volatile int*)(&num_j)) = 0;
                tot_j = 0;
            }
            
            jlba += item->block_header->size;
            item = next;
        }
    }
    while (*((volatile int*)(&num_j)) < tot_j) {
        block_idle(qp);
        // wait for all journal writes to finish
    }
    LOG_INFO("Finished writing journal items\n");
    num_j = 0;
    tot_j = 0;
    for (i = 0; i < SUFS_MAX_CPU; i++) {
        struct tfs_journal_header *jh = &(journal->jhs[i]);
        struct tfs_journal_item *item = jh->head;
        while (item) {
            struct tfs_journal_item *next = item->nxt;
            
            tfs_chainhash_remove(J_HASH_TO_HASH(journal->map_[i]),
                                 (unsigned long)item->block_header->buf, 0, 0);
            tot_j++;
            LOG_INFO("Writing journal item %p, lba %lx, size %d\n", item,
                     item->block_header->lba, item->block_header->size);
            write_blk_async(qp, item->block_header->lba,
                            item->block_header->size, item->block_header->buf,
                            journal_async_callback, &num_j);
            if (tot_j>=64){
                while (*((volatile int*)(&num_j)) < tot_j) {
                    block_idle(qp);
                    // wait for all journal writes to finish
                }
                *((volatile int*)(&num_j)) = 0;
                tot_j = 0;
            }
            free(item->block_header);
            free(item);
            item = next;
        }
        jh->head = NULL;
    }
    while (*((volatile int*)(&num_j)) < tot_j) {
        block_idle(qp);
        // wait for all journal writes to finish
    }
    tfs_bravo_write_unlock(&(journal->rw_lock));
}

void tfs_fini_journal(struct tfs_journal *journal) {
    int i;
    for(i = 0;i < SUFS_MAX_CPU; i++){
        tfs_chainhash_forced_remove_and_kill(J_HASH_TO_HASH(journal->map_[i]));
    }
    free(journal->jhs);
    journal->jhs = NULL;
    tfs_bravo_rwlock_destroy(&(journal->rw_lock));
}