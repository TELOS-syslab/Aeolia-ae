/* SPDX-License-Identifier: GPL-2.0 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_eevdf.bpf.skel.h"
#include "scx_eevdf.h"
#include <time.h>

static bool verbose;
static volatile int exit_req;
// static volatile struct scx_rq_ctx qctxs[NUM_POSSIBLE_CPUS];

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sigint_handler(int eevdf)
{
	exit_req = 1;
}

// static void print_scx_rq_ctxs(struct scx_eevdf *skel)
// {
// 	volatile struct scx_rq_ctx *qctx;
// 	int ret, cpu, idx = 0;
// 	ret = bpf_map_lookup_elem(bpf_map__fd(skel->maps.scx_rq_ctx_stor), &idx,
// 				  (void *)qctxs);

// 	time_t now;
// 	time(&now);
// 	printf("[%s]", ctime(&now));

// 	if (ret) {
// 		printf("Failed to open maps!\n");
// 		return;
// 	}

// 	for (cpu = 0; cpu < NUM_POSSIBLE_CPUS; cpu++) {
// 		qctx = qctxs + cpu;
// 		printf("cpu : %d,\tavg_vruntime : %ld,\t\t\tavg_load : %lu,\t\t\tmin_vruntime : %lu\n",
// 		       cpu, qctx->avg_vruntime, qctx->avg_load,
// 		       qctx->min_vruntime);
// 	}
// }

int main(int argc, char **argv)
{
	struct scx_eevdf *skel;
	struct bpf_link *link;
	__u64 ecode;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

restart:
	skel = SCX_OPS_OPEN(eevdf_ops, scx_eevdf);

	SCX_OPS_LOAD(skel, eevdf_ops, scx_eevdf, uei);
	link = SCX_OPS_ATTACH(skel, eevdf_ops, scx_eevdf);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		sleep(1);
	}

	unlink(USER_TASK_CTX_FILE);

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_eevdf__destroy(skel);

	if (UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}