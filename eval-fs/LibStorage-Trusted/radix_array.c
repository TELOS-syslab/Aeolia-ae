#include <stdio.h>
#include <string.h>

#include "./include/logger.h"
#include "./include/radix_array.h"

static void tfs_radix_array_init_upper_node(struct tfs_radix_array *ra,
                                            struct tfs_ra_node **upper_ptr) {
    int size =
        sizeof(struct tfs_ra_node) + (ra->upper_fanout) * sizeof(unsigned long);

    if (posix_memalign((void **)upper_ptr, 8, size) != 0) {
        LOG_ERROR("posix_memalign failed");
        exit(EXIT_FAILURE);
    }

    memset((void *)(*upper_ptr), 0, size);

    (*upper_ptr)->child =
        (unsigned long *)((char *)(*upper_ptr) + sizeof(unsigned long *));
}

void tfs_init_radix_array(struct tfs_radix_array *ra, size_t t_size, size_t n,
                          size_t node_bytes) {
    ra->n = n;

    /* both upper_fanout and leaf_fanout is 512 */
    ra->upper_fanout = node_bytes / sizeof(struct tfs_ra_node_ptr);
    ra->leaf_fanout = node_bytes / tfs_round_up_to_pow2_const(t_size);

    /* both upper_bits and leaf_bits is 9 */
    ra->upper_bits = tfs_log2_exact(ra->upper_fanout, 0);
    ra->leaf_bits = tfs_log2_exact(ra->leaf_fanout, 0);

    /* ra->levels is 5 */
    ra->levels = tfs_ra_num_levels(ra, 0) - 1;

    ra->root_ = 0;

#if 0
      tfs_radix_array_init_upper_node(ra,
            (struct tfs_ra_node**) &ra->root_);
#endif
}

unsigned long tfs_radix_array_find(struct tfs_radix_array *ra, size_t index,
                                   int fill, unsigned long ps) {
    struct tfs_ra_node_ptr node;
    struct tfs_ra_node *unode = NULL;
    unsigned int node_level = 0;
    size_t subkey;

    if (ra->root_ == 0) {
        tfs_radix_array_init_upper_node(ra, (struct tfs_ra_node **)&ra->root_);
    }

    if (index > ra->n)
        index = ra->n;

    node.v = ra->root_;

    for (node_level = ra->levels; node_level > 0; --node_level) {
        struct tfs_ra_node_ptr next;

        unode = tfs_ra_ptr_to_node(&node);

        subkey = tfs_ra_subkey(ra, index, node_level);

        next.v = unode->child[subkey];

        if (tfs_ra_get_type(&next) == TFS_RA_TYPE_NONE) {
            if (!fill) {
                return 0;
            } else {
                tfs_radix_array_init_upper_node(
                    ra, (struct tfs_ra_node **)&unode->child[subkey]);

                unode->child[subkey] = (unode->child[subkey] | TFS_RA_TYPE_SET);

                next.v = unode->child[subkey];
            }
        }

        node = next;
    }

    unode = tfs_ra_ptr_to_node(&node);
    subkey = tfs_ra_subkey(ra, index, 0);

    if (fill) {
        unode->child[subkey] = ps;
    }

    return unode->child[subkey];
}

void tfs_radix_array_free_recursive(struct tfs_radix_array *ra,
                                    unsigned long addr, int level) {
    struct tfs_ra_node_ptr node;
    struct tfs_ra_node *unode = NULL;
    int i = 0;

    if (addr == 0)
        return;

    node.v = addr;

    unode = tfs_ra_ptr_to_node(&node);

    for (i = 0; i < ra->upper_fanout; i++) {
        struct tfs_ra_node_ptr next;

        next.v = unode->child[i];

        if (tfs_ra_get_type(&next) != TFS_RA_TYPE_NONE) {
            if (level > 1)
                tfs_radix_array_free_recursive(ra, next.v, level - 1);

            unode->child[i] = 0;
        }
    }

    /* Do not free the root */
    if (level != ra->levels)
        free(unode);

    return;
}
