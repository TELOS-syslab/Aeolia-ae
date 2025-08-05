#ifndef TFS_TLS_H_
#define TFS_TLS_H_

#include <stdlib.h>
#include <string.h>

#include "libfs-headers.h"

#define TFS_STACK_SIZE 8 * 1024 * 1024 // 8MB

struct tfs_tls_state {
    u32 uid;
    u32 gid;
    // protected
    ls_nvme_qp *qp;

    ls_nvme_dev *nvme;
    ls_queue_args *qp_args;
    ls_device_args *dev_args;

    unsigned long stack_addr;

    // TODO: padding
};

extern __thread int tls_my_thread;

extern struct tfs_tls_state *tfs_tls_state;
extern atomic_int tfs_tid;

int tfs_init_tls(int tid);

int tfs_tls_my_tid(void);

u32 tfs_tls_uid(void);
u32 tfs_tls_gid(void);

ls_nvme_dev *tfs_tls_ls_nvme_dev(void);

ls_nvme_qp *tfs_tls_ls_nvme_qp(void);

static inline void block_idle(ls_nvme_qp *qp) {
    //yield();
    ls_qpair_process_completions(qp, 0);
}

#endif // TFS_TLS_H_