#ifndef SCX_EEVDF_H
#define SCX_EEVDF_H

// Common for driver & kernel module & user application
#ifdef __KERNEL__
#include <linux/types.h>
#elif !defined(__BPF__) && !defined(__VMLINUX_H__)
// Only include stdint.h and define types when not in BPF context
#include <stdint.h>
typedef int64_t s64;
typedef int32_t s32;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#endif

// Define TICK_NSEC for BPF context (typical value is 1000000 ns = 1ms)
#ifndef TICK_NSEC
#define TICK_NSEC 1000000ULL
#endif

// Define max macro for userspace
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/*
 * Integer metrics need fixed point arithmetic, e.g., sched/fair
 * has a few: load, load_avg, util_avg, freq, and capacity.
 *
 * We define a basic fixed point arithmetic range, and then formalize
 * all these metrics based on that basic range.
 */
#define SCHED_FIXEDPOINT_SHIFT 10
#define SCHED_FIXEDPOINT_SCALE (1L << SCHED_FIXEDPOINT_SHIFT)

/*
 * Increase resolution of nice-level calculations for 64-bit architectures.
 * The extra resolution improves shares distribution and load balancing of
 * low-weight task groups (eg. nice +19 on an autogroup), deeper task-group
 * hierarchies, especially on larger systems. This is not a user-visible change
 * and does not change the user-interface for setting shares/weights.
 *
 * We increase resolution only if we have enough bits to allow this increased
 * resolution (i.e. 64-bit). The costs for increasing resolution when 32-bit
 * are pretty high and the returns do not justify the increased costs.
 *
 * Really only required when CONFIG_FAIR_GROUP_SCHED=y is also set, but to
 * increase coverage and consistency always enable it on 64-bit platforms.
 */
#ifdef CONFIG_64BIT
#define NICE_0_LOAD_SHIFT (SCHED_FIXEDPOINT_SHIFT + SCHED_FIXEDPOINT_SHIFT)
#define scale_load(w)	  ((w) << SCHED_FIXEDPOINT_SHIFT)
#define scale_load_down(w)                                             \
	({                                                             \
		u32 __w = (w);                               \
                                                                       \
		if (__w)                                               \
			__w = max(2UL, __w >> SCHED_FIXEDPOINT_SHIFT); \
		__w;                                                   \
	})
#else
#define NICE_0_LOAD_SHIFT  (SCHED_FIXEDPOINT_SHIFT)
#define scale_load(w)	   (w)
#define scale_load_down(w) (w)
#endif

/*
 * Task weight (visible to users) and its load (invisible to users) have
 * independent resolution, but they should be well calibrated. We use
 * scale_load() and scale_load_down(w) to convert between them. The
 * following must be true:
 *
 *  scale_load(sched_prio_to_weight[NICE_TO_PRIO(0)-MAX_RT_PRIO]) == NICE_0_LOAD
 *
 */
#define NICE_0_LOAD (1L << NICE_0_LOAD_SHIFT)

#define WMULT_CONST (~0U)
#define WMULT_SHIFT 32

/* FIXME: We currently hard-codes NUMA related parameters. */
#define NUM_NUMA_NODES 4
#define NUM_NODE_CPUS	  32
#define NUM_POSSIBLE_CPUS NUM_NUMA_NODES * NUM_NODE_CPUS
#define MAX_NUM_THREADS	  4096
#define VPID_BITMAP_SIZE  MAX_NUM_THREADS >> 3

#define BPF_FS_DIR	   "/sys/fs/bpf"
#define USER_RQ_CTX_FILE	   BPF_FS_DIR "/scx_rq_ctx_stor"
#define USER_TASK_CTX_FILE BPF_FS_DIR "/user_task_ctx_stor"

struct user_load_weight {
	unsigned long			weight;
	uint32_t				inv_weight;
};

struct user_task_ctx {
    int pid;
	int nice;
	struct user_load_weight load;
	uint32_t cpu;

	uint64_t exec_start;
	uint64_t vruntime;
	uint64_t deadline;
	uint64_t slice;
};

struct user_scx_rq_ctx {
	uint32_t cpu;
    uint32_t cand_nice;
	struct user_load_weight load;
	uint32_t nr_running;
	int64_t avg_vruntime;
	uint64_t avg_load;
	uint64_t min_vruntime;
};

extern void* scx_rq_ctx_map;
extern void* user_task_ctx_map;


int need_reschedule(struct user_task_ctx *uctx);
struct user_task_ctx* scx_find_task_ctx(int pid);
void scx_mmap_user(void);
void scx_unmap_user(void);
#endif