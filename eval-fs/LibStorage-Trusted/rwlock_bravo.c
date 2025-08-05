#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "./include/bravo.h"
#include "./include/rwlock_bravo.h"
#include "./include/util.h"

void tfs_bravo_read_lock(struct tfs_bravo_rwlock *l) {
    unsigned long now_time;
    int slot;

    if (l->rbias) {
        slot = tfs_bravo_hash((unsigned long)l);

        if (__sync_bool_compare_and_swap(&tfs_global_vr_table[slot], NULL, l)) {
            if (l->rbias) {
                return;
            }

            tfs_global_vr_table[slot] = NULL;
        }
    }

    /* slow-path */
    tfs_rwticket_rdlock(&l->underlying);

    now_time = tfs_rdtsc();

    if (l->rbias == false && now_time >= l->inhibit_until) {
        l->rbias = true;
    }
}

void tfs_bravo_read_unlock(struct tfs_bravo_rwlock *l) {
    int slot = 0;

    slot = tfs_bravo_hash((unsigned long)l);

    if (tfs_global_vr_table[slot] != NULL) {
        tfs_global_vr_table[slot] = NULL;
    } else {
        tfs_rwticket_rdunlock(&l->underlying);
    }
}

void tfs_bravo_write_lock(struct tfs_bravo_rwlock *l) {
    tfs_rwticket_wrlock(&l->underlying);

    if (l->rbias) {
        unsigned long start_time = 0, now_time = 0, i = 0;

        l->rbias = false;

        start_time = tfs_rdtsc();

        for (i = 0; i < TFS_RL_NUM_SLOT; i++) {
            while (tfs_global_vr_table[i] == (unsigned long *)l)
                ;
        }

        now_time = tfs_rdtsc();

        l->inhibit_until = now_time + ((now_time - start_time) * TFS_BRAVO_N);
    }
}
