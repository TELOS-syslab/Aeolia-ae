#ifndef SCX_EEVDF_H
#define SCX_EEVDF_H

/* FIXME: We currently hard-codes NUMA related parameters. */
#define NUM_NUMA_NODES 4
#define NUM_NODE_CPUS	  32
#define NUM_POSSIBLE_CPUS NUM_NUMA_NODES * NUM_NODE_CPUS
#define MAX_NUM_THREADS	  4096
#define VPID_BITMAP_SIZE  MAX_NUM_THREADS >> 3

#define NSEC_PER_SEC (1000000000UL)
#define HZ	     250
/* TICK_NSEC is the time between ticks in nsec assuming SHIFTED_HZ */
#define TICK_NSEC ((NSEC_PER_SEC + HZ / 2) / HZ)

#define BPF_FS_DIR	   "/sys/fs/bpf"
#define TASK_CTX_FILE	   BPF_FS_DIR "/task_ctx_stor"
#define USER_TASK_CTX_FILE BPF_FS_DIR "/user_task_ctx_stor"

/* Task context used in user space 
 * @vpid: virtual task identification allocated by BPF scheduler
 * @cpu: cpu the task is running on
 * @nr_running: number of runnable and running tasks of @cpu's rq
 */
struct user_task_ctx {
	int vpid;
	int nice;
	uint32_t cpu;
	uint32_t nr_running;

	uint64_t exec_start;
	uint64_t vruntime;
	uint64_t deadline;

	int on_rq;

	/* Used for rescheduling */
	int need_resched;
	int cand_nice;
	uint64_t cand_deadline;
};

inline void test_and_clear_need_resched(struct user_task_ctx *uctx);

#endif
