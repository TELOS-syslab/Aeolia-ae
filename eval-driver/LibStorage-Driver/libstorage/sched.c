#include "scx_eevdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include <bpf/libbpf.h>
#include "logger.h"

// Define likely/unlikely macros for userspace
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

void *scx_rq_ctx_map = NULL;
void *user_task_ctx_map = NULL;

void scx_mmap_user(void){
    int map_fd;
    map_fd = bpf_obj_get(USER_RQ_CTX_FILE);
    if (map_fd < 0) {
        perror("bpf_obj_get scx_rq_ctx_stor");
        return 1;
    }

    scx_rq_ctx_map = mmap(NULL, NUM_POSSIBLE_CPUS * sizeof(struct user_scx_rq_ctx), PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
    if (scx_rq_ctx_map == MAP_FAILED) {
        perror("mmap scx_rq_ctx_stor");
        return 1;
    }

    map_fd = bpf_obj_get(USER_TASK_CTX_FILE);
    if (map_fd < 0) {
        perror("bpf_obj_get user_task_ctx_stor");
        return 1;
    }

    user_task_ctx_map = mmap(NULL, MAX_NUM_THREADS * sizeof(struct user_task_ctx),
                             PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
    if (user_task_ctx_map == MAP_FAILED) {
        perror("mmap user_task_ctx_stor");
        return 1;
    }

}

void scx_unmap_user(void) {
    if (scx_rq_ctx_map) {
        munmap(scx_rq_ctx_map, NUM_POSSIBLE_CPUS * sizeof(struct user_scx_rq_ctx));
    }
    if (user_task_ctx_map) {
        munmap(user_task_ctx_map, MAX_NUM_THREADS * sizeof(struct user_task_ctx));
    }
}

struct user_task_ctx* scx_find_task_ctx(int pid) {
    struct user_task_ctx *uctx;

    if (user_task_ctx_map == NULL) {
        perror("user_task_ctx_map is NULL");
        return NULL;
    }

    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        uctx = (struct user_task_ctx *)((char *)user_task_ctx_map + i * sizeof(struct user_task_ctx));
        if (uctx->pid == pid) {
            return uctx;
        }
    }
    return NULL;
}

static inline u64 mul_u64_u32_shr(u64 a, u32 mul, s32 shift)
{
	u32 ah, al;
	u64 ret;

	al = a;
	ah = a >> 32;

	ret = ((u64)al * mul) >> shift;
	if (ah)
		ret += ((u64)ah * mul) << (32 - shift);

	return ret;
}

static inline void __update_inv_weight(struct user_load_weight *lw)
{
	u32 w;

	if (likely(lw->inv_weight))
		return;

	w = lw->weight;

	if (unlikely(w >= WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = WMULT_CONST;
	else
		lw->inv_weight = WMULT_CONST / w;
}

static inline u64 __calc_delta(u64 delta_exec, u32 weight, struct user_load_weight *lw)
{
	u64 fact = weight;
	s32 shift = WMULT_SHIFT;

	__update_inv_weight(lw);

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * lw->inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}

/*
 * delta /= w
 */
static inline u64 calc_delta_fair(u64 delta, struct user_task_ctx *uctx)
{
	if (unlikely(uctx->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &uctx->load);

	return delta;
}

/*
 * XXX: strictly: vd_i += N*r_i/w_i such that: vd_i > ve_i
 * this is probably good enough.
 */
static inline int update_deadline(struct user_scx_rq_ctx *qctx,
			    struct user_task_ctx *uctx)
{
#define SCX_SLICE_DFL 20000000ULL

	if ((s64)(uctx->vruntime - uctx->deadline) < 0)
		return false;

	/*
	 * EEVDF: vd_i = ve_i + r_i / w_i
	 */
	uctx->deadline = uctx->vruntime + calc_delta_fair(SCX_SLICE_DFL, uctx);

	/*
     * The task has consumed its request, reschedule.
     */
	if (qctx->nr_running > 1)
		return true;

	return false;
}

static __always_inline u64 min_u64(u64 x, u64 y)
{
	return x < y ? x : y;
}

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    // Use 'volatile' to prevent compiler reordering
    __asm__ volatile (
        "rdtsc" : "=a" (lo), "=d" (hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

int need_reschedule(struct user_task_ctx *uctx)
{
	if (!uctx || !scx_rq_ctx_map)
		return true;

	u32 cpu = sched_getcpu();
	struct user_scx_rq_ctx *qctx = (struct user_scx_rq_ctx *)((char *)scx_rq_ctx_map + cpu * sizeof(struct user_scx_rq_ctx));

	if (qctx->nr_running <= 1)
		return false;

	// int resched = false;
	// s64 delta_exec = rdtsc() - uctx->exec_start;

	// if (unlikely(delta_exec <= 0))
	// 	return true;

	// uctx->slice -= min_u64(uctx->slice, delta_exec);
	// uctx->vruntime += calc_delta_fair(delta_exec, uctx);
	// resched = update_deadline(qctx, uctx) || uctx->slice == 0;

	return true;
}