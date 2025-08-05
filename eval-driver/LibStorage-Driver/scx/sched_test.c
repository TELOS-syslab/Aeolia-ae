#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <bpf/libbpf.h>

#define MAX_NUM_THREADS 4096
#define NUM_POSSIBLE_CPUS 128

typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;

struct load_weight {
	unsigned long			weight;
	u32				inv_weight;
};

struct scx_rq_ctx {
	u32 cpu;
    u32 cand_nice;
	struct load_weight load;
	u32 nr_running;
	s64 avg_vruntime;
	u64 avg_load;
	u64 min_vruntime;
};

struct user_task_ctx {
    int pid;
	int nice;
	uint32_t cpu;

	uint64_t exec_start;
	uint64_t vruntime;
	uint64_t deadline;
};

int main()
{
    int map_fd, i;
    void *scx_rq_ctx_map = NULL;
    void *user_task_ctx_map = NULL;

    int cur_pid = getpid();
    printf("Current PID: %d\n", cur_pid);

    map_fd = bpf_obj_get("/sys/fs/bpf/scx_rq_ctx_stor");
    if (map_fd < 0) {
        perror("bpf_obj_get scx_rq_ctx_stor");
        return 1;
    }

    scx_rq_ctx_map = mmap(NULL, NUM_POSSIBLE_CPUS * sizeof(struct scx_rq_ctx), PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
    if (scx_rq_ctx_map == MAP_FAILED) {
        perror("mmap scx_rq_ctx_stor");
        return 1;
    }

    map_fd = bpf_obj_get("/sys/fs/bpf/user_task_ctx_stor");
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

    printf("=== scx_rq_ctx_stor[0] ===\n");
    struct scx_rq_ctx *qctx = (struct scx_rq_ctx *)scx_rq_ctx_map;
    for (i = 0; i < 128; i++) {
        printf("cpu[%d]: cpu = %u, cand_nice = %u, nr_running = %u, "
               "avg_vruntime = %ld, avg_load = %lu, min_vruntime = %lu\n",
               i, qctx[i].cpu, qctx[i].cand_nice, qctx[i].nr_running,
               qctx[i].avg_vruntime, qctx[i].avg_load, qctx[i].min_vruntime);
    }

    printf("=== user_task_ctx_stor ===\n");
    struct user_task_ctx *uctx = (struct user_task_ctx *)user_task_ctx_map;
    for (i = 0; i < MAX_NUM_THREADS; i++) {
        if (uctx[i].pid == cur_pid) {
            printf("pid = %d, nice = %d, cpu = %u, "
                   "exec_start = %lu, vruntime = %lu, deadline = %lu\n",
                   uctx[i].pid, uctx[i].nice, uctx[i].cpu,
                   uctx[i].exec_start, uctx[i].vruntime, uctx[i].deadline);
            break;
        }
    }

    munmap(scx_rq_ctx_map, NUM_POSSIBLE_CPUS * sizeof(struct scx_rq_ctx));
    munmap(user_task_ctx_map, MAX_NUM_THREADS * sizeof(struct user_task_ctx));
    return 0;
}