#ifndef TFS_HASH_H_
#define TFS_HASH_H_

#include <stdint.h>

static inline uint64_t tfs_hash_int(uint64_t v) {
    uint64_t x = v ^ (v >> 32) ^ (v >> 20) ^ (v >> 12);
    return x ^ (x >> 7) ^ (x >> 4);
}

static inline uint64_t tfs_hash_uint64(uint64_t key) {
    uint64_t h = 0;
    char* string = (char*)&key;
    int max_size = sizeof(uint64_t);
    for (int i = 0; i < max_size; i++) {
        uint64_t c = string[i];
        /* Lifted from dcache.h in Linux v3.3 */
        h = (h + (c << 4) + (c >> 4)) * 11;
    }
    return h;
}

#endif /* SUFS_HASH_H_ */
