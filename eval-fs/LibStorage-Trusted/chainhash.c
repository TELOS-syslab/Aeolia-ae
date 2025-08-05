#include <stdbool.h>
#include <stdio.h>

#include "./include/chainhash.h"
#include "./include/hash.h"
#include "./include/seqlock.h"

#define TFS_HASH_SIZES_MAX 15

static const long tfs_hash_sizes[TFS_HASH_SIZES_MAX] = {
    1063,   2153,   4363,    8219,    16763,   32957,   64601,   128983,
    256541, 512959, 1024921, 2048933, 4096399, 8192003, 16384001};

const long tfs_hash_min_size = tfs_hash_sizes[0];

const long tfs_hash_max_size =
    tfs_hash_sizes[TFS_HASH_SIZES_MAX - 1];

/*
 * value : ino
 * value2: ptr to struct sufs_dir_entry
 */
static inline void tfs_ch_item_init(struct tfs_ch_item *item,
                                           uint64_t key,
                                           unsigned long value,
                                           unsigned long value2) {
    item->key = key;

#if 0
    printf("key is %s, item key is %s\n", key, item->key);
#endif

    item->val = value;
    item->val2 = value2;
}

static inline void tfs_ch_item_free(struct tfs_ch_item *item) {

    if (item)
        free(item);
}

static inline void
tfs_chainhash_bucket_init(struct tfs_ch_bucket *bucket) {
    pthread_spin_init(&(bucket->lock), PTHREAD_PROCESS_SHARED);

    bucket->head = NULL;
    bucket->dead_ = false;
}

void tfs_chainhash_init(struct tfs_chainhash *hash, int index) {
    int i = 0;

    if (index < 0 || index >= TFS_HASH_SIZES_MAX) {
        fprintf(stderr, "index :%d for hash table is too large!\n", index);
        abort();
    }

    uint64_t nbuckets = tfs_hash_sizes[index];

    hash->nbuckets_ = nbuckets;
    hash->buckets_ =
        malloc(hash->nbuckets_ * sizeof(struct tfs_ch_bucket));

    hash->nbuckets_resize_ = 0;
    hash->buckets_resize_ = NULL;

    hash->dead_ = false;
    hash->size = 0;

    tfs_seq_lock_init(&(hash->seq_lock));

    for (i = 0; i < hash->nbuckets_; i++) {
        tfs_chainhash_bucket_init(&(hash->buckets_[i]));
    }
}

struct tfs_ch_bucket *
tfs_chainhash_find_resize_buckets(struct tfs_chainhash *hash,
                                         uint64_t key) {
    struct tfs_ch_bucket *buckets = NULL;
    uint64_t nbuckets = 0;
    struct tfs_ch_bucket *b = NULL;

    buckets = hash->buckets_resize_;
    nbuckets = hash->nbuckets_resize_;

    /* This can happen due to the completion of resize */
    while (buckets == NULL || nbuckets == 0) {
        buckets = hash->buckets_;
        nbuckets = hash->nbuckets_;
    }

    b = &(buckets[tfs_hash_uint64(key) % nbuckets]);

    return b;
}

static struct tfs_ch_bucket *
tfs_get_buckets(struct tfs_chainhash *hash, uint64_t key) {
    int bseq = 0, eseq = 0;
    struct tfs_ch_bucket *buckets = NULL;
    uint64_t nbuckets = 0;

    do {
        bseq = tfs_seq_lock_read(&(hash->seq_lock));

        buckets = hash->buckets_;
        nbuckets = hash->nbuckets_;

        eseq = tfs_seq_lock_read(&(hash->seq_lock));

    } while (tfs_seq_lock_retry(bseq, eseq));

    return (&(buckets[tfs_hash_uint64(key) % nbuckets]));
}

bool tfs_chainhash_lookup(struct tfs_chainhash *hash, uint64_t key, unsigned long *vptr,
                                 unsigned long *vptr2) {
    struct tfs_ch_item *i = NULL;
    struct tfs_ch_bucket *b = NULL;
    bool ret;

    b = tfs_get_buckets(hash, key);

#if 0
    pthread_spin_lock(&b->lock);
#endif

    if (b->dead_) {
#if 0
        pthread_spin_unlock(&b->lock);
#endif

        b = tfs_chainhash_find_resize_buckets(hash, key);

#if 0
        pthread_spin_lock(&b->lock);
#endif
    }

    for (i = b->head; i != NULL; i = i->next) {
        if (i->key!=key)
            continue;

        if (vptr)
            (*vptr) = i->val;

        if (vptr2)
            (*vptr2) = i->val2;

        ret = true;
        goto out;
    }

    ret = false;

out:
#if 0
    pthread_spin_unlock(&b->lock);
#endif
    return ret;
}

static unsigned long
tfs_chainhash_new_size(struct tfs_chainhash *hash, int enlarge) {
    int i = 0;
    for (i = 0; i < TFS_HASH_SIZES_MAX; i++) {
        if (tfs_hash_sizes[i] == hash->nbuckets_)
            break;
    }

    if (enlarge) {
        if (i == TFS_HASH_SIZES_MAX) {
            fprintf(stderr, "Hash reaches maximum size!\n");
            return 0;
        }

        i++;
    } else {
        if (i == 0) {
            fprintf(stderr, "Bug: reducing the size of a minimum hash!\n");
            return 0;
        }

        i--;
    }

    return tfs_hash_sizes[i];
}

static void tfs_chainhash_resize(struct tfs_chainhash *hash,
                                        int enlarge) {
    int i = 0;

#if 0
    return;
#endif

    /* The resize is already in progress, return */
    if (!__sync_bool_compare_and_swap(&hash->nbuckets_resize_, 0, 1))
        return;

    hash->nbuckets_resize_ = tfs_chainhash_new_size(hash, enlarge);

    if (!hash->nbuckets_resize_) {
        return;
    }

    /* init the resize hash */
    hash->buckets_resize_ =
        malloc(hash->nbuckets_resize_ * sizeof(struct tfs_ch_bucket));

    if (!hash->buckets_resize_) {
        fprintf(stderr, "Cannot malloc the new hash table");
        abort();
    }

    for (i = 0; i < hash->nbuckets_resize_; i++) {
        tfs_chainhash_bucket_init(&(hash->buckets_resize_[i]));
    }

    /* move the entry in the old hash to the new one */
    for (i = 0; i < hash->nbuckets_; i++) {
        struct tfs_ch_item *iter = NULL;
        struct tfs_ch_bucket *b = NULL;

        b = &(hash->buckets_[i]);

        pthread_spin_lock(&(b->lock));

        b->dead_ = true;

        iter = hash->buckets_[i].head;
        while (iter) {
            struct tfs_ch_item *prev = NULL;
            struct tfs_ch_bucket *nb = NULL;

            prev = iter;
            iter = iter->next;

            nb = &hash->buckets_resize_[tfs_hash_uint64(prev->key) %
                                        hash->nbuckets_resize_];


            pthread_spin_lock(&(nb->lock));

            prev->next = nb->head;
            nb->head = prev;

            pthread_spin_unlock(&(nb->lock));
        }

        hash->buckets_[i].head = NULL;

        pthread_spin_unlock(&(b->lock));
    }

    /* swap back */
    tfs_seq_lock_write_begin(&hash->seq_lock);

    hash->buckets_ = hash->buckets_resize_;
    hash->nbuckets_ = hash->nbuckets_resize_;

    tfs_seq_lock_write_end(&hash->seq_lock);

    hash->buckets_resize_ = NULL;

    /*
     * This seems like both a compiler and a hardware fence
     * https://stackoverflow.com/questions/982129/what-does-sync-synchronize-do
     */

    /* May not need this given x86 is TSO */
    /* __sync_synchronize(); */

    hash->nbuckets_resize_ = 0;

    return;
}

// TODO: Implement val3 to store page*
bool tfs_chainhash_insert(struct tfs_chainhash *hash, uint64_t key, unsigned long val,
                                 unsigned long val2,
                                 struct tfs_ch_item **item) {
    bool ret = false;
    struct tfs_ch_item *i = NULL;
    struct tfs_ch_bucket *b = NULL;

    if (hash->dead_) {
        return false;
    }

    b = tfs_get_buckets(hash, key);

    pthread_spin_lock(&b->lock);

    if (hash->dead_) {

        ret = false;
        goto out;
    }

    if (b->dead_) {
        pthread_spin_unlock(&b->lock);

        b = tfs_chainhash_find_resize_buckets(hash, key);

        pthread_spin_lock(&b->lock);
    }

    for (i = b->head; i != NULL; i = i->next) {
        if (i->key == key) {

            ret = false;
            goto out;
        }
    }

    i = malloc(sizeof(struct tfs_ch_item));


    tfs_ch_item_init(i, key, val, val2);

    i->next = b->head;
    b->head = i;

    ret = true;

    if (item)
        *item = i;

    hash->size++;

out:
    pthread_spin_unlock(&b->lock);

    /* TODO: Make the test a function.. */
    if (ret && hash->nbuckets_ != tfs_hash_max_size &&
        hash->size > hash->nbuckets_ * TFS_DIR_REHASH_FACTOR) {
        tfs_chainhash_resize(hash, 1);
    }

#if 0
    printf("hash size upon insert :%ld\n", hash->size);
#endif

    return ret;
}

bool tfs_chainhash_remove(struct tfs_chainhash *hash, uint64_t key,
                                 unsigned long *val,
                                 unsigned long *val2) {
    bool ret = false;

    struct tfs_ch_bucket *b = NULL;
    struct tfs_ch_item *i = NULL, *prev = i;

    b = tfs_get_buckets(hash, key);

    pthread_spin_lock(&b->lock);

    if (b->dead_) {
        pthread_spin_unlock(&b->lock);

        b = tfs_chainhash_find_resize_buckets(hash, key);

        pthread_spin_lock(&b->lock);
    }

    for (i = b->head; i != NULL; i = i->next) {
        if (i->key==key) {
            if (prev == NULL) {
                b->head = i->next;
            } else {
                prev->next = i->next;
            }

            ret = true;
            hash->size--;
            goto out;
        }

        prev = i;
    }

out:
    pthread_spin_unlock(&b->lock);

    if (ret) {
        if (val) {
            (*val) = i->val;
        }

        if (val2) {
            (*val2) = i->val2;
        }

        // tfs_ch_item_free(i);
    }

    /* TODO: Make the test a function.. */
    if (ret && hash->nbuckets_ != tfs_hash_min_size &&
        hash->size * TFS_DIR_REHASH_FACTOR < hash->nbuckets_) {
        tfs_chainhash_resize(hash, 0);
    }

    return ret;
}


bool tfs_chainhash_remove_and_kill(struct tfs_chainhash *hash) {
    if (hash->dead_ || hash->size != 0)
        return false;

    hash->dead_ = true;

    return true;
}

/* This will not be executed concurrently with resize so we are good */
void tfs_chainhash_forced_remove_and_kill(
    struct tfs_chainhash *hash) {
    int i = 0;

    for (i = 0; i < hash->nbuckets_; i++)
        pthread_spin_lock(&hash->buckets_[i].lock);

    for (i = 0; i < hash->nbuckets_; i++) {
        struct tfs_ch_item *iter = hash->buckets_[i].head;
        while (iter) {
            struct tfs_ch_item *prev = iter;

            /* This breaks all the abstractions. Oh, well... */
            // tfs_inode_clear_allocated(iter->val);

            iter = iter->next;
            tfs_ch_item_free(prev);
        }
    }

    hash->dead_ = true;

    for (i = 0; i < hash->nbuckets_; i++)
        pthread_spin_unlock(&hash->buckets_[i].lock);
}

