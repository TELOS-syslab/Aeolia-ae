#ifndef TFS_SEQLOCK_H_
#define TFS_SEQLOCK_H_

#include <stdatomic.h>


static inline void tfs_seq_lock_init(atomic_int *seqlock) {
    (*seqlock) = 0;
}

static inline void tfs_seq_lock_write_begin(atomic_int *seqlock) {
    (*seqlock)++;
}

static inline void tfs_seq_lock_write_end(atomic_int *seqlock) {
    (*seqlock)++;
}

static inline int tfs_seq_lock_read(atomic_int *seqlock) {
    return (*seqlock);
}

static inline int tfs_seq_lock_retry(int bseg, int eseg) {
    return ((bseg != eseg) || (bseg % 2 != 0));
}

#endif
