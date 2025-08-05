#ifndef TFS_TRAMPOLINE_H_
#define TFS_TRAMPOLINE_H_

#include "../include/trusted-states.h"
#include "mpk.h"
#include "tls.h"

extern struct tfs_state *tfs_state;

int trampoline(int (*func)(unsigned long), unsigned long arg,
               void *new_stack_ptr);

static inline int trampoline_call(int (*func)(unsigned long),
                                  unsigned long arg) {
    // LOG_INFO("trampoline_call: %p\n", func);
    int ret = 0;
    int tid = tfs_tls_my_tid();
    // LOG_INFO("trampoline_call: %d\n", tid);
    enter_protected_region(tfs_state->in_trust);
    // LOG_INFO("enter_protected region\n");

    // unsafe
    ret = trampoline(func, arg, (void *)tfs_tls_state[tid].stack_addr);
    // func(arg);

    exit_protected_region(tfs_state->out_trust);
    // LOG_INFO("exit_protected region\n");
    return ret;
}

#endif // TFS_TRAMPOLINE_H_