#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "./include/bravo.h"

volatile unsigned long **tfs_global_vr_table;

// OK
void tfs_init_global_rglock_bravo(void) {
    tfs_global_vr_table = calloc(TFS_RL_TABLE_SIZE, sizeof(unsigned long *));
}

// OK
void tfs_free_global_rglock_bravo(void) {
    if (tfs_global_vr_table)
        free(tfs_global_vr_table);

    tfs_global_vr_table = NULL;
}
