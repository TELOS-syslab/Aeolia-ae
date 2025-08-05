#ifndef TFS_CHAINHASH_H_
#define TFS_CHAINHASH_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "compiler.h"

#define TFS_DIR_INIT_HASH_IDX 0

#define TFS_GDIR_INIT_HASH_IDX 14

#define TFS_DIR_REHASH_FACTOR 2

struct tfs_ch_item {
    struct tfs_ch_item *next;

    uint64_t key;
    unsigned long val;
    unsigned long val2;
};

struct tfs_ch_bucket {
    pthread_spinlock_t lock __mpalign__;
    bool dead_;

    struct tfs_ch_item *head;
};

struct tfs_chainhash {
    uint64_t nbuckets_;
    struct tfs_ch_bucket *buckets_;

    uint64_t nbuckets_resize_;
    struct tfs_ch_bucket *buckets_resize_;

    atomic_long size;
    atomic_int seq_lock;
    bool dead_;
};

static inline bool
tfs_chainhash_killed(struct tfs_chainhash *hash) {
    return hash->dead_;
}

static inline void
tfs_chainhash_fini(struct tfs_chainhash *hash) {
    if (hash->buckets_)
        free(hash->buckets_);

    hash->dead_ = true;
    hash->buckets_ = NULL;
}

void tfs_chainhash_init(struct tfs_chainhash *hash, int index);

bool tfs_chainhash_lookup(struct tfs_chainhash *hash, uint64_t key, unsigned long *vptr1,
                                 unsigned long *vptr2);

bool tfs_chainhash_insert(struct tfs_chainhash *hash, uint64_t key, unsigned long val,
                                 unsigned long val2,
                                 struct tfs_ch_item **item);

bool tfs_chainhash_remove(struct tfs_chainhash *hash, uint64_t key, unsigned long *val,
                                 unsigned long *val2);

// bool tfs_chainhash_replace_from(struct tfs_chainhash *dst,
//                                        char *kdst, unsigned long dst_exist,
//                                        struct tfs_chainhash *src,
//                                        char *ksrc, unsigned long vsrc,
//                                        unsigned long vsrc2, int max_size,
//                                        struct tfs_ch_item **item);

bool tfs_chainhash_remove_and_kill(struct tfs_chainhash *hash);

void tfs_chainhash_forced_remove_and_kill(
    struct tfs_chainhash *hash);

// bool tfs_chainhash_enumerate(struct tfs_chainhash *hash,
//                                     int max_size, char *prev, char *out);

#endif /* CHAINHASH_H_ */
