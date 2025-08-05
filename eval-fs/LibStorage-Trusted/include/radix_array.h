#ifndef TFS_RADIX_ARRAY_H_
#define TFS_RADIX_ARRAY_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define TFS_RA_LOCK_BIT 2
#define TFS_RA_TYPE_MASK (3 << 0)
#define TFS_RA_LOCK_MASK (1 << TFS_RA_LOCK_BIT)
#define TFS_RA_MASK (TFS_RA_TYPE_MASK | TFS_RA_LOCK_MASK)

#define TFS_RA_TYPE_NONE 0
#define TFS_RA_TYPE_SET 1

/* need to think of the synchronization issue here */
struct tfs_ra_node {
    unsigned long *child;
};

struct tfs_ra_node_ptr {
    uintptr_t v;
};

struct tfs_radix_array {
    unsigned int levels;

    size_t upper_bits;
    size_t leaf_bits;

    size_t upper_fanout;
    size_t leaf_fanout;

    size_t n;

    unsigned long root_;
};

static inline size_t tfs_ceil_log2_const(size_t x, bool exact) {
    return (x == 0)   ? (1 / x)
           : (x == 1) ? (exact ? 0 : 1)
                      : 1 + tfs_ceil_log2_const(x >> 1,
                                                ((x & 1) == 1) ? false : exact);
}

static inline size_t tfs_round_up_to_pow2_const(size_t x) {
    return (size_t)1 << tfs_ceil_log2_const(x, true);
}

static inline size_t tfs_log2_exact(size_t x, size_t accum) {
    return (x == 0)         ? (1 / x)
           : (x == 1)       ? accum
           : ((x & 1) == 0) ? tfs_log2_exact(x >> 1, accum + 1)
                            : ~0;
}

static inline size_t tfs_ra_key_shift(struct tfs_radix_array *ra,
                                      unsigned level) {
    return level == 0 ? 0 : ra->leaf_bits + ((level - 1) * ra->upper_bits);
}

static inline size_t tfs_ra_key_mask(struct tfs_radix_array *ra,
                                     unsigned level) {
    return level == 0 ? (ra->leaf_fanout - 1) : (ra->upper_fanout - 1);
}

static inline size_t tfs_ra_level_span(struct tfs_radix_array *ra,
                                       unsigned level) {
    return (size_t)1 << tfs_ra_key_shift(ra, level);
}

static inline unsigned int tfs_ra_num_levels(struct tfs_radix_array *ra,
                                             unsigned level) {
    return tfs_ra_level_span(ra, level) >= ra->n
               ? level
               : tfs_ra_num_levels(ra, level + 1);
}

static inline struct tfs_ra_node *
tfs_ra_ptr_to_node(struct tfs_ra_node_ptr *ptr) {
    return (struct tfs_ra_node *)(ptr->v & ~TFS_RA_MASK);
}

static inline unsigned int tfs_ra_subkey(struct tfs_radix_array *ra, size_t k,
                                         unsigned level) {
    return (k >> tfs_ra_key_shift(ra, level)) & tfs_ra_key_mask(ra, level);
}

static inline unsigned int tfs_ra_get_type(struct tfs_ra_node_ptr *ptr) {
    return (ptr->v & TFS_RA_MASK);
}

static inline void tfs_free_radix_array(struct tfs_radix_array *ra) {
    free((void *)ra->root_);
}

void tfs_init_radix_array(struct tfs_radix_array *ra, size_t t_size, size_t n,
                          size_t node_bytes);

unsigned long tfs_radix_array_find(struct tfs_radix_array *ra, size_t index,
                                   int fill, unsigned long ps);

void tfs_radix_array_free_recursive(struct tfs_radix_array *ra,
                                    unsigned long addr, int level);

static inline void tfs_radix_array_fini(struct tfs_radix_array *ra) {
    tfs_radix_array_free_recursive(ra, ra->root_, ra->levels);
}

#endif
