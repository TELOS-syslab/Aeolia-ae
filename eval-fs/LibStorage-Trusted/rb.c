#include <errno.h>
#include <stdio.h>

#include "./include/compiler.h"
#include "./include/logger.h"
#include "./include/rb.h"

int tfs_rbtree_find_range_node(struct rb_root *tree, unsigned long key,
                               enum node_type type,
                               struct tfs_range_node **ret_node) {
    struct tfs_range_node *curr = NULL;
    struct rb_node *temp = NULL;
    int compVal = 0, ret = 0;

    temp = tree->rb_node;

    while (temp) {
        curr = container_of(temp, struct tfs_range_node, node);
        compVal = tfs_rbtree_compare_range_node(curr, key, type);

        if (compVal == -1) {
            temp = temp->rb_left;
        } else if (compVal == 1) {
            temp = temp->rb_right;
        } else {
            ret = 1;
            break;
        }
    }

    if (ret_node)
        *ret_node = curr;

    return ret;
}

int tfs_rbtree_insert_range_node(struct rb_root *tree,
                                 struct tfs_range_node *new_node,
                                 enum node_type type) {
    struct tfs_range_node *curr = NULL;
    struct rb_node **temp = NULL, *parent = NULL;
    int compVal;

    temp = &(tree->rb_node);

    while (*temp) {
        curr = container_of(*temp, struct tfs_range_node, node);
        compVal =
            tfs_rbtree_compare_range_node(curr, new_node->range_low, type);
        parent = *temp;

        if (compVal == -1) {
            temp = &((*temp)->rb_left);
        } else if (compVal == 1) {
            temp = &((*temp)->rb_right);
        } else {
            LOG_ERROR("%s: entry %lu - %lu already exists: "
                      "%lu - %lu\n",
                      __func__, new_node->range_low, new_node->range_high,
                      curr->range_low, curr->range_high);
            return -EINVAL;
        }
    }

    rb_link_node(&new_node->node, parent, temp);
    rb_insert_color(&new_node->node, tree);

    return 0;
}

void tfs_rbtree_destroy_range_node_tree(struct rb_root *tree) {
    struct tfs_range_node *curr = NULL;
    struct rb_node *temp = NULL;

    temp = rb_first(tree);
    while (temp) {
        curr = container_of(temp, struct tfs_range_node, node);
        temp = rb_next(temp);
        rb_erase(&curr->node, tree);
        tfs_free_range_node(curr);
    }
}
