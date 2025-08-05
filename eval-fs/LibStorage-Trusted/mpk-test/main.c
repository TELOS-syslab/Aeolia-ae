#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trampoline.h"

struct main_state *tfs_state;
struct main_tls_state *tfs_tls_state;

unsigned long st_tsc = 0;
unsigned long mid_tsc = 0;
unsigned long end_tsc = 0;

struct func_arg {
    unsigned long a;
};

int fun(){
    return 122;
}

int __real_func(unsigned long arg) {
    mid_tsc = tfs_rdtsc();
    struct func_arg *arg_ptr = (struct func_arg *)arg;
    arg_ptr->a = fun();
    // printf("Hello from the real function! Arg: %lu\n", arg_ptr->a);
    return 50;
}

unsigned long call_func(unsigned long arg) {
    // printf("Calling function with arg: %lu\n", ((struct func_arg *)arg)->a);
    return trampoline_call(__real_func, arg);
}

int main(void) {
    printf("Starting main function\n");
    tfs_state = malloc(sizeof(struct main_state));
    printf("main_state: %p\n", tfs_state);
    tfs_state->pkey = allocate_pkey();
    printf("Allocated pkey: %d\n", tfs_state->pkey);

    // if (tfs_state->pkey < 0) {
    //     abort();
    // }
    int tot = 10000000;
    st_tsc = tfs_rdtsc();
    int i;
    for (i = 0; i < tot; i++) {
        tfs_state->out_trust = __rdpkru();
    }
    mid_tsc = tfs_rdtsc();
    printf("st_tsc: %lu, mid_tsc: %lu\n", st_tsc, mid_tsc);
    printf("avg time: %lu\n", (mid_tsc - st_tsc) / tot);

    st_tsc = tfs_rdtsc();
    for (i = 0; i < tot; i++) {
        __wrpkrumem(tfs_state->out_trust);
    }
    mid_tsc = tfs_rdtsc();
    printf("st_tsc: %lu, mid_tsc: %lu\n", st_tsc, mid_tsc);
    printf("avg time: %lu\n", (mid_tsc - st_tsc) / tot);

    // tfs_state->out_trust = __rdpkru();
    // tfs_state->in_trust = tfs_state->out_trust & ~(3 << (tfs_state->pkey * 2));

    return 0 ;

    tfs_tls_state = malloc(sizeof(struct main_tls_state) * 10);
    unsigned long ret = 0;
    ret = call_func((unsigned long)&(struct func_arg){.a = 100});
    if (ret != 50) {
        printf("Error: expected 50, got %ld, %ld\n", ret, ret);
    } else {
        printf("Success: got expected return value\n");
    }
    printf("st_tsc: %lu, mid_tsc: %lu, end_tsc: %lu\n", st_tsc, mid_tsc, end_tsc);
    printf("s-m: %lu, m-e: %lu, s-e: %lu\n", mid_tsc-st_tsc, end_tsc-mid_tsc, end_tsc-st_tsc);

    int t = 10;
    while(t--){
        ret = call_func((unsigned long)&(struct func_arg){.a = 100});
        if (ret != 50) {
            printf("Error: expected 50, got %ld, %ld\n", ret, ret);
        } else {
            printf("Success: got expected return value\n");
        }
        printf("st_tsc: %lu, mid_tsc: %lu, end_tsc: %lu\n", st_tsc, mid_tsc, end_tsc);
        printf("s-m: %lu, m-e: %lu, s-e: %lu\n", mid_tsc-st_tsc, end_tsc-mid_tsc, end_tsc-st_tsc);
    }

    free(tfs_tls_state);
    free(tfs_state);

    return 0;
}