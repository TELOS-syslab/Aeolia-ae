#ifndef TFS_RB_H_
#define TFS_RB_H_

#include <stdlib.h>

#include "rbtree.h"

/* Range node type */
enum node_type {
    NODE_BLOCK = 1,
    NODE_INODE,
    NODE_DIR,
};

/* A node in the RB tree representing a range of pages */
struct tfs_range_node {
    struct rb_node node;
    union {
        /* Block, inode */
        struct {
            unsigned long range_low;
            unsigned long range_high;
        };
        /* Dir node */
        struct {
            unsigned long hash;
            void *direntry;
        };
    };
};

static inline struct tfs_range_node *tfs_alloc_range_node(void) {
    struct tfs_range_node *p;

    p = (struct tfs_range_node *)calloc(1, sizeof(struct tfs_range_node));

    return p;
}

static inline void tfs_free_range_node(struct tfs_range_node *node) {
    free(node);
}

static inline struct tfs_range_node *tfs_alloc_blocknode(void) {
    return tfs_alloc_range_node();
}

static inline void tfs_free_blocknode(struct tfs_range_node *node) {
    tfs_free_range_node(node);
}

static inline struct tfs_range_node *tfs_alloc_inode_node(void) {
    return tfs_alloc_range_node();
}

static inline void tfs_free_inode_node(struct tfs_range_node *node) {
    tfs_free_range_node(node);
}

static inline int tfs_rbtree_compare_range_node(struct tfs_range_node *curr,
                                                unsigned long key,
                                                enum node_type type) {
    if (type == NODE_DIR) {
        if (key < curr->hash)
            return -1;
        if (key > curr->hash)
            return 1;
        return 0;
    }
    /* Block and inode */
    if (key < curr->range_low)
        return -1;
    if (key > curr->range_high)
        return 1;

    return 0;
}

int tfs_rbtree_find_range_node(struct rb_root *tree, unsigned long key,
                               enum node_type type,
                               struct tfs_range_node **ret_node);

int tfs_rbtree_insert_range_node(struct rb_root *tree,
                                 struct tfs_range_node *new_node,
                                 enum node_type type);

void tfs_rbtree_destroy_range_node_tree(struct rb_root *tree);

#endif
