#ifndef TFS_JOURNAL_H_
#define TFS_JOURNAL_H_

#include <pthread.h>

#include "libfs-headers.h"
#include "rwlock_bravo.h"
#include "chainhash.h"

struct tfs_block_header {
    int size;
    unsigned long lba;
    dma_buffer *buf;
};

struct __attribute__((aligned(64))) tfs_journal_item {
    union{
        struct {
            struct tfs_block_header *block_header;
            struct tfs_journal_item *nxt;
        };
    char padding[64];
    };
};

struct __attribute__((aligned(64))) tfs_journal_header {
    union{
        struct {
            int journal_tail;
            unsigned long journal_lba_head;
            struct tfs_journal_item *head;
        };
        char padding[64];
    };
};

struct __attribute__((aligned(64))) tfs_jouranl_chainhash {
    union {
        struct {
            struct tfs_chainhash map;
        };
        char padding[64]; 
    };
    
};

struct __attribute__((aligned(64))) tfs_journal {
    union{
        struct {
            struct tfs_bravo_rwlock rw_lock;
            struct tfs_journal_header *jhs;
        };
        char padding[64];
    };
    struct tfs_jouranl_chainhash map_[SUFS_MAX_CPU];
};

void tfs_init_journal(struct tfs_journal *journal);
void tfs_fini_journal(struct tfs_journal *journal);

void tfs_run_journal(struct tfs_journal *journal, unsigned long lba, int size,
                     dma_buffer *buf);

void tfs_exit_journal(struct tfs_journal *journal);

void tfs_commit_and_checkpoint_journal(struct tfs_journal *journal);
void tfs_begin_journal(struct tfs_journal *journal);

#endif // TFS_JOURNAL_H_