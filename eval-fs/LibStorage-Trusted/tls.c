#include "./include/tls.h"
#include "./include/logger.h"
#include "./include/mpk.h"
#include "./include/trusted-states.h"

#include "../LibStorage-Driver/libstorage/command_types.h"

__thread int tls_my_thread = -1;

struct tfs_tls_state *tfs_tls_state = NULL;
atomic_int tfs_tid = 0;

int tfs_init_tls(int tid) {
    int result = 0;
    tfs_tls_state[tid].uid = getuid();
    tfs_tls_state[tid].gid = getgid();
    tfs_tls_state[tid].qp = malloc(sizeof(ls_nvme_qp));
    tfs_tls_state[tid].nvme = malloc(sizeof(ls_nvme_dev));
    tfs_tls_state[tid].qp_args = malloc(sizeof(ls_queue_args));
    tfs_tls_state[tid].dev_args = malloc(sizeof(ls_device_args));
    tfs_tls_state[tid].stack_addr = (unsigned long)malloc(TFS_STACK_SIZE);

    memset(tfs_tls_state[tid].qp, 0, sizeof(ls_nvme_qp));
    memset(tfs_tls_state[tid].nvme, 0, sizeof(ls_nvme_dev));
    memset(tfs_tls_state[tid].qp_args, 0, sizeof(ls_queue_args));
    memset(tfs_tls_state[tid].dev_args, 0, sizeof(ls_device_args));
    memset((void *)tfs_tls_state[tid].stack_addr, 0, TFS_STACK_SIZE);

    tfs_tls_state[tid].stack_addr += (TFS_STACK_SIZE - 16);
    tfs_tls_state[tid].stack_addr &= ~(16 - 1);

    strcpy(tfs_tls_state[tid].dev_args->path, "/dev/nvme0");
    result = open_device(tfs_tls_state[tid].dev_args, tfs_tls_state[tid].nvme);
    if (result < 0) {
        LOG_ERROR("Failed to open device\n");
        return -1;
    }
    
    // Create queue pair
    tfs_tls_state[tid].qp_args->nid = tfs_tls_state[tid].nvme->id;
    tfs_tls_state[tid].qp_args->interrupt = LS_INTERRPUT_POLLING;
    tfs_tls_state[tid].qp_args->depth = 128;
    tfs_tls_state[tid].qp_args->num_requests = 512;
    tfs_tls_state[tid].qp_args->upid_idx =
        tfs_tls_state[tid].dev_args->upid_idx;
    tfs_tls_state[tid].qp_args->instance_id = 
        tfs_tls_state[tid].dev_args->instance_id;
    // TODO: set queue priority
    // if (opts->type) {
    //     //	qp_args->flags |= NVME_SQ_PRIO_HIGH;
    // } else {
    //     qp_args->flags |= NVME_SQ_PRIO_LOW;
    // }
    result = create_qp(tfs_tls_state[tid].qp_args, tfs_tls_state[tid].nvme,
                       tfs_tls_state[tid].qp, tfs_state->in_trust, tfs_state->out_trust);
    if (result < 0) {
        LOG_ERROR("Failed to create qp\n");
        return -1;
    }

    // Protect buffers
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].stack_addr),
                             TFS_STACK_SIZE, tfs_state->pkey);
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].qp->cqes),
                             tfs_tls_state[tid].qp->depth *
                                 sizeof(struct nvme_completion),
                             tfs_state->pkey);
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].qp->sqes),
                             tfs_tls_state[tid].qp->depth *
                                 sizeof(struct nvme_command),
                             tfs_state->pkey);
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].qp->iods),
                             tfs_tls_state[tid].qp->depth * sizeof(io_request),
                             tfs_state->pkey);
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].qp->req_buf),
                             tfs_tls_state[tid].qp->depth * sizeof(io_request),
                             tfs_state->pkey);
    protect_buffer_with_pkey((void *)(tfs_tls_state[tid].qp->dbs),
                             2 * SUFS_PAGE_SIZE, tfs_state->pkey);

    return 0;
}

int tfs_tls_my_tid(void) {
    int ret = tls_my_thread;

    if (ret == -1) {
        tls_my_thread = atomic_fetch_add(&tfs_tid, 1);
        ret = tls_my_thread;
    }

    if (ret >= SUFS_MAX_THREADS) {
        LOG_ERROR("Too many threads\n");
        abort();
    }

    return ret;
}

ls_nvme_dev *tfs_tls_ls_nvme_dev(void) {
    return tfs_tls_state[tfs_tls_my_tid()].nvme;
}

ls_nvme_qp *tfs_tls_ls_nvme_qp(void) {
    return tfs_tls_state[tfs_tls_my_tid()].qp;
}

u32 tfs_tls_uid(void) { return tfs_tls_state[tfs_tls_my_tid()].uid; }
u32 tfs_tls_gid(void) { return tfs_tls_state[tfs_tls_my_tid()].gid; }