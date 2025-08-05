#ifndef TFS_ATOMIC_UTIL_H_
#define TFS_ATOMIC_UTIL_H_

#include <stdbool.h>

static inline bool tfs_cmpxch_bool(bool *ptr, bool expected, bool desired) {
    return __atomic_compare_exchange_n(ptr, &expected, desired, 1,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#define tfs_atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define tfs_atomic_inc(P) __sync_add_and_fetch((P), 1)

#endif /* TFS_ATOMIC_UTIL_H_ */
