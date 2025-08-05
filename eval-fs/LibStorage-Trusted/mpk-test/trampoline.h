#ifndef TFS_TRAMPOLINE_H_
#define TFS_TRAMPOLINE_H_

#include "../include/mpk.h"
#include <stdatomic.h>

struct main_state {
    int pkey;
    uint32_t in_trust;
    uint32_t out_trust;
};

struct main_tls_state {
    unsigned long stack_addr;
};

extern struct main_state *tfs_state;
extern struct main_tls_state *tfs_tls_state;

extern unsigned long st_tsc;
extern unsigned long mid_tsc;
extern unsigned long end_tsc;

static inline long long tfs_rdtsc(void) {
    unsigned long hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (lo | (hi << 32));
}

unsigned long trampoline(int (*func)(unsigned long), unsigned long arg,
                         void *new_stack_ptr);

__thread int tls_my_thread = -1;

atomic_int tfs_tid = 0;

int tfs_init_tls(int tid) {
    int result = 0;
    printf("tfs_init_tls: %d\n", tid);
    tfs_tls_state[tid].stack_addr =
        (unsigned long)mmap(NULL, 8 * 1024 * 1024, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

    memset((void *)tfs_tls_state[tid].stack_addr, 0, 8 * 1024 * 1024);

    printf("tfs_tls_state[tid].stack_addr: %p\n",
           (void *)tfs_tls_state[tid].stack_addr);

    tfs_tls_state[tid].stack_addr += (8 * 1024 * 1024 - 16);
    tfs_tls_state[tid].stack_addr &= ~(16 - 1);

    protect_buffer_with_pkey((void *)tfs_tls_state[tid].stack_addr,
                             8 * 1024 * 1024, tfs_state->pkey);
    return 0;
}

int tfs_tls_my_tid(void) {
    int ret = tls_my_thread;

    if (ret == -1) {
        tls_my_thread = atomic_fetch_add(&tfs_tid, 1);
        tfs_init_tls(tls_my_thread);
        ret = tls_my_thread;
    }

    if (ret >= 10) {
        abort();
    }

    return ret;
}

static inline unsigned long trampoline_call(int (*func)(unsigned long),
                                            unsigned long arg) {
    // printf("trampoline_call: %p\n", func);
    unsigned long ret = 0;
    int tid = tfs_tls_my_tid();
    // printf("trampoline_call tid: %d\n", tid);

    st_tsc = tfs_rdtsc();
    enter_protected_region(tfs_state->in_trust);

    // unsafe
    ret = trampoline(func, arg, (void *)tfs_tls_state[tid].stack_addr);

    exit_protected_region(tfs_state->out_trust);
    end_tsc = tfs_rdtsc();
    return ret;
}

#endif // TFS_TRAMPOLINE_H_