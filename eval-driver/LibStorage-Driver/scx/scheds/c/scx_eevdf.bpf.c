/* SPDX-License-Identifier: GPL-2.0 */
/*
 * eevdf CPU scheduler
 */
#include <scx/common.bpf.h>
#include <string.h>

// Define TICK_NSEC for BPF context (typical value is 1000000 ns = 1ms)
#ifndef TICK_NSEC
#define TICK_NSEC 1000000ULL
#endif

// Include the header after BPF context is established to avoid typedef conflicts
#include "scx_eevdf.h"

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

#ifdef __VMLINUX_H__
#define MAX_NICE   19
#define MIN_NICE   -20
#define NICE_WIDTH (MAX_NICE - MIN_NICE + 1)

/*
 * Priority of a process goes from 0..MAX_PRIO-1, valid RT
 * priority is 0..MAX_RT_PRIO-1, and SCHED_NORMAL/SCHED_BATCH
 * tasks are in the range MAX_RT_PRIO..MAX_PRIO-1. Priority
 * values are inverted: lower p->prio value means higher priority.
 */

#define MAX_RT_PRIO 100
#define MAX_DL_PRIO 0

#define MAX_PRIO     (MAX_RT_PRIO + NICE_WIDTH)
#define DEFAULT_PRIO (MAX_RT_PRIO + NICE_WIDTH / 2)

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice) ((nice) + DEFAULT_PRIO)
#define PRIO_TO_NICE(prio) ((prio) - DEFAULT_PRIO)
#endif /* __KERNEL__ */

struct scx_rq_ctx {
	u32 cpu;
    u32 cand_nice;
	struct load_weight load;
	u32 nr_running;
	s64 avg_vruntime;
	u64 avg_load;
	u64 min_vruntime;
};

struct vpid_bitmap {
	struct bpf_spin_lock lock;
	u8 bitmap[VPID_BITMAP_SIZE];
	u32 count;
    u32 last_idx;
};

struct task_ctx {
	u32 cpu;
	u32 vpid;
	struct load_weight load;
	u64 deadline;
	u64 min_vruntime;
	bool on_rq;
	u64 exec_start;
	u64 sum_exec_runtime;
	u64 prev_sum_exec_runtime;
	u64 vruntime;
	s64 vlag;
	u64 slice;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, struct scx_rq_ctx);
	__uint(max_entries, NUM_POSSIBLE_CPUS);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __uint(map_flags, BPF_F_MMAPABLE);
} scx_rq_ctx_stor SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, struct vpid_bitmap);
	__uint(max_entries, 1);
} vpid_bitmap_stor SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, s32);
	__type(value, struct task_ctx);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
} task_ctx_stor SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, struct user_task_ctx);
	__uint(max_entries, MAX_NUM_THREADS);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
	__uint(map_flags, BPF_F_MMAPABLE);
} user_task_ctx_stor SEC(".maps");

static __always_inline u64 eligible_dsq(u32 cpu)
{
	return 2 * cpu;
}

static __always_inline u64 ineligible_dsq(u32 cpu)
{
	return 2 * cpu + 1;
}

static inline struct scx_rq_ctx *try_lookup_scx_rq_ctx(s32 cpu)
{
	u32 idx = cpu;
	struct scx_rq_ctx *qctx =
		bpf_map_lookup_elem(&scx_rq_ctx_stor, &idx);
	if (unlikely(!qctx))
		scx_bpf_error("Failed to retrieve cpu context!");

	return qctx;
}

static __always_inline struct scx_rq_ctx *this_scx_rq_ctx()
{
	s32 cpu = bpf_get_smp_processor_id();
	return try_lookup_scx_rq_ctx(cpu);
}

static s32 try_request_vpid()
{
	u32 idx = 0;
	struct vpid_bitmap *vpid_bitmap =
		bpf_map_lookup_elem(&vpid_bitmap_stor, &idx);
	if (unlikely(!vpid_bitmap)) {
		scx_bpf_error("Failed to retrieve vpid bitmap!");
		return -ENOENT;
	}

	s32 vpid = -1;
	u8 offset;
	u8 bitmap;
	bpf_spin_lock(&vpid_bitmap->lock);
	// for (idx = vpid_bitmap->last_idx; idx < VPID_BITMAP_SIZE; idx++) {
		
	for (idx = 0; idx < VPID_BITMAP_SIZE; idx++) {
		bitmap = vpid_bitmap->bitmap[idx];
		for (offset = 0; offset < 8; offset++) {
			if (!(bitmap & (1 << offset))) {
				vpid_bitmap->count++;
				vpid = 8 * idx + offset;
				bitmap |= (1 << offset);
				vpid_bitmap->bitmap[idx] = bitmap;
				goto out;
			}
		}
	}
out:
    // vpid_bitmap->last_idx = idx;
    // if (vpid_bitmap->last_idx >= VPID_BITMAP_SIZE) {
    //     vpid_bitmap->last_idx = 0; // Reset for next round
    // }
	bpf_spin_unlock(&vpid_bitmap->lock);

	if (unlikely(vpid < 0)) {
		scx_bpf_error(
			"Failed to request vpid %d! allocated vpid number : %d",
			vpid, vpid_bitmap->count);
		return -EBUSY;
	}

	return vpid;
}

static void try_release_vpid(s32 vpid)
{
	if (unlikely(vpid < 0)) {
		scx_bpf_error("Invalid vpid value %d!", vpid);
		return;
	}

	u32 idx = 0;
	struct vpid_bitmap *vpid_bitmap =
		bpf_map_lookup_elem(&vpid_bitmap_stor, &idx);
	if (unlikely(!vpid_bitmap)) {
		scx_bpf_error("Failed to retrieve vpid bitmap!");
		return;
	}

	idx = vpid >> 3;
	if (unlikely(idx >= VPID_BITMAP_SIZE)) {
		scx_bpf_error("Invalid vpid bitmap index %u!", idx);
		return;
	}

	u8 offset = vpid % 8;
	bpf_spin_lock(&vpid_bitmap->lock);
	if (unlikely(idx >= VPID_BITMAP_SIZE)) {
		goto out;
	}
	vpid_bitmap->bitmap[idx] &= ~(1 << offset);
	vpid_bitmap->count--;
out:
	bpf_spin_unlock(&vpid_bitmap->lock);
}

static inline struct task_ctx *try_lookup_task_ctx(struct task_struct *p)
{
	struct task_ctx *tctx;
	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);

	return tctx;
}

static inline struct task_struct *curr_task(struct scx_rq_ctx *qctx)
{
	u32 cpu = qctx->cpu;
	struct rq *rq = scx_bpf_cpu_rq(cpu);

	struct task_struct *p = rq->curr;
	if (unlikely(!p)) {
		return NULL;
	}

	return p;
}

static inline struct task_ctx *curr_task_ctx(struct scx_rq_ctx *qctx)
{
	struct task_struct *p = curr_task(qctx);
	if (unlikely(!p)) {
		return NULL;
	}

	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return NULL;

	return tctx;
}

static inline struct user_task_ctx *
try_lookup_user_task_tcx(struct task_ctx *tctx)
{
	struct user_task_ctx *uctx;
	u32 idx = tctx->vpid;
	uctx = bpf_map_lookup_elem(&user_task_ctx_stor, &idx);
	if (unlikely(!uctx))
		scx_bpf_error("Failed to retrieve user task context!");

	return uctx;
}

static inline void update_user_task_ctx(struct user_task_ctx *uctx,
					struct task_ctx *tctx)
{
	uctx->load.weight = tctx->load.weight;
	uctx->load.inv_weight = tctx->load.inv_weight;
	uctx->cpu = tctx->cpu;
	uctx->exec_start = tctx->exec_start;
	uctx->vruntime = tctx->vruntime;
	uctx->deadline = tctx->deadline;
	uctx->slice = tctx->slice;
}

static __always_inline u64 min_u64(u64 x, u64 y)
{
	return x < y ? x : y;
}

static __always_inline u64 max_u64(u64 x, u64 y)
{
	return x < y ? y : x;
}

static __always_inline s64 clamp_s64(s64 curr, s64 lo, s64 hi)
{
	if (curr < lo)
		return lo;
	if (curr > hi)
		return hi;
	return curr;
}

static __always_inline u64 mul_u64_u32_shr(u64 a, u32 mul, s32 shift)
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

static __always_inline s64 div_s64(s64 dividend, s32 divisor)
{
	s32 sign = 1;
	if (dividend < 0)
		sign *= -1;
	if (divisor < 0)
		sign *= -1;

	u64 abs_dividend = dividend < 0 ? -dividend : dividend;
	u32 abs_divisor = divisor < 0 ? -divisor : divisor;

	u64 rslt = abs_dividend / abs_divisor;
	return sign * rslt;
}

static __always_inline s32 task_nice(struct task_struct *p)
{
	if (!p)
		return MAX_NICE;
	return PRIO_TO_NICE(p->prio);
}


static inline void update_rq_cand(struct scx_rq_ctx *qctx,
                  struct task_struct *p){
    if(task_nice (p) < qctx->cand_nice){
        qctx->cand_nice = task_nice(p);
    }
}

static __always_inline s64 task_key(struct scx_rq_ctx *qctx,
				    struct task_ctx *tctx)
{
	return (s64)(tctx->vruntime - qctx->min_vruntime);
}
/*
 * Compute virtual time from the per-task service numbers:
 *
 * Fair schedulers conserve lag:
 *
 *   \Sum lag_i = 0
 *
 * Where lag_i is given by:
 *
 *   lag_i = S - s_i = w_i * (V - v_i)
 *
 * Where S is the ideal service time and V is it's virtual time counterpart.
 * Therefore:
 *
 *   \Sum lag_i = 0
 *   \Sum w_i * (V - v_i) = 0
 *   \Sum w_i * V - w_i * v_i = 0
 *
 * From which we can solve an expression for V in v_i (which we have in
 * task->vruntime):
 *
 *       \Sum v_i * w_i   \Sum v_i * w_i
 *   V = -------------- = --------------
 *          \Sum w_i            W
 *
 * Specifically, this is the weighted average of all task virtual runtimes.
 *
 * [[ NOTE: this is only equal to the ideal scheduler under the condition
 *          that join/leave operations happen at lag_i = 0, otherwise the
 *          virtual time has non-contiguous motion equivalent to:
 *
 *	      V +-= lag_i / W
 *
 *	    Also see the comment in place_task() that deals with this. ]]
 *
 * However, since v_i is u64, and the multiplication could easily overflow
 * transform it into a relative form that uses smaller quantities:
 *
 * Substitute: v_i == (v_i - v0) + v0
 *
 *     \Sum ((v_i - v0) + v0) * w_i   \Sum (v_i - v0) * w_i
 * V = ---------------------------- = --------------------- + v0
 *                  W                            W
 *
 * Which we track using:
 *
 *                    v0 := eevdf_rq->min_vruntime
 * \Sum (v_i - v0) * w_i := eevdf_rq->avg_vruntime
 *              \Sum w_i := eevdf_rq->avg_load
 *
 * Since min_vruntime is a monotonic increasing variable that closely tracks
 * the per-task service, these deltas: (v_i - v), will be in the order of the
 * maximal (virtual) lag induced in the system due to quantisation.
 *
 * Also, we use scale_load_down() to reduce the size.
 *
 * As measured, the max (key * weight) value was ~44 bits for a kernel build.
 */
static inline void avg_vruntime_add(struct scx_rq_ctx *qctx,
				    struct task_ctx *tctx)
{
	u64 weight = scale_load_down(tctx->load.weight);
	s64 key = task_key(qctx, tctx);

	qctx->avg_vruntime += key * weight;
	qctx->avg_load += weight;
}

static inline void avg_vruntime_sub(struct scx_rq_ctx *qctx,
				    struct task_ctx *tctx)
{
	u64 weight = scale_load_down(tctx->load.weight);
	s64 key = task_key(qctx, tctx);

	qctx->avg_vruntime -= key * weight;
	qctx->avg_load -= weight;
}

static __always_inline void avg_vruntime_update(struct scx_rq_ctx *qctx,
						s64 delta)
{
	/*
     * v' = v + d ==> avg_vruntime' = avg_runtime - d*avg_load
     */
	qctx->avg_vruntime -= qctx->avg_load * delta;
}

/*
 * Specifically: avg_runtime() + 0 must result in task_eligible() := true
 * For this to be so, the result of this function must have a left bias.
 */
u64 avg_vruntime(struct scx_rq_ctx *qctx)
{
	if (unlikely(!qctx)) {
		scx_bpf_error("Invalid scx rq context!");
		return 0;
	}

	struct task_ctx *curr = curr_task_ctx(qctx);
	s64 avg = qctx->avg_vruntime;
	s64 load = qctx->avg_load;

	if (curr && curr->on_rq) {
		u64 weight = scale_load_down(curr->load.weight);

		avg += task_key(qctx, curr) * weight;
		load += weight;
	}

	if (load) {
		/* sign flips effective floor / ceiling */
		if (avg < 0)
			avg -= (load - 1);
		avg = div_s64(avg, load);
	}

	return qctx->min_vruntime + avg;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline u64 __update_min_vruntime(struct scx_rq_ctx *qctx, u64 vruntime)
{
	u64 min_vruntime = qctx->min_vruntime;
	/*
	 * open coded max_vruntime() to allow updating avg_vruntime
	 */
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta > 0) {
		avg_vruntime_update(qctx, delta);
		min_vruntime = vruntime;
	}
	return min_vruntime;
}

static inline void update_load_add(struct load_weight *lw, u32 inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

static inline void update_load_sub(struct load_weight *lw, u32 dec)
{
	lw->weight += dec;
	lw->inv_weight = 0;
}

static inline void update_load_set(struct load_weight *lw, u32 w)
{
	lw->weight = w;
	lw->inv_weight = 0;
}

static inline void __update_inv_weight(struct load_weight *lw)
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

static inline u64 __calc_delta(u64 delta_exec, u32 weight,
			       struct load_weight *lw)
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
static inline u64 calc_delta_fair(u64 delta, struct task_ctx *tctx)
{
	if (unlikely(tctx->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &tctx->load);

	return delta;
}

/*
 * Task is eligible once it received less service than it ought to have,
 * eg. lag >= 0.
 *
 * lag_i = S - s_i = w_i*(V - v_i)
 *
 * lag_i >= 0 -> V >= v_i
 *
 *     \Sum (v_i - v)*w_i
 * V = ------------------ + v
 *          \Sum w_i
 *
 * lag_i >= 0 -> \Sum (v_i - v)*w_i >= (v_i - v)*(\Sum w_i)
 *
 * Note: using 'avg_vruntime() > task->vruntime' is inaccurate due
 *       to the loss in precision caused by the division.
 */
static s32 vruntime_eligible(struct scx_rq_ctx *qctx, u64 vruntime)
{
	struct task_ctx *curr = curr_task_ctx(qctx);
	s64 avg = qctx->avg_vruntime;
	s64 load = qctx->avg_load;

	if (curr && curr->on_rq) {
		u64 weight = scale_load_down(curr->load.weight);

		avg += task_key(qctx, curr) * weight;
		load += weight;
	}

	return avg >= (s64)(vruntime - qctx->min_vruntime) * load;
}

static __always_inline s32 task_eligible(struct scx_rq_ctx *qctx,
					 struct task_ctx *tctx)
{
	return vruntime_eligible(qctx, tctx->vruntime);
}

/*
 * lag_i = S - s_i = w_i * (V - v_i)
 *
 * However, since V is approximated by the weighted average of all entities it
 * is possible -- by addition/removal/reweight to the tree -- to move V around
 * and end up with a larger lag than we started with.
 *
 * Limit this to either double the slice length with a minimum of TICK_NSEC
 * since that is the timing granularity.
 *
 * EEVDF gives the following limit for a steady state system:
 *
 *   -r_max < lag < max(r_max, q)
 *
 * XXX could add max_slice to the augmented data to track this.
 */
static __always_inline s64 task_lag(u64 avruntime, struct task_ctx *tctx)
{
	s64 vlag, limit;

	vlag = avruntime - tctx->vruntime;
	limit = calc_delta_fair(max_u64(2 * tctx->slice, TICK_NSEC), tctx);

	return clamp_s64(vlag, -limit, limit);
}

static __always_inline void update_task_lag(struct scx_rq_ctx *qctx,
					    struct task_ctx *tctx)
{
	tctx->vlag = task_lag(avg_vruntime(qctx), tctx);
}

static inline struct task_struct *pick_first_task(struct scx_rq_ctx *qctx)
{
	u32 cpu = qctx->cpu;
	struct bpf_iter_scx_dsq it = {};
	s32 ret;

	ret = bpf_iter_scx_dsq_new(&it, eligible_dsq(cpu), 0);
	if (unlikely(ret < 0)) {
		scx_bpf_error("Failed to allocate iterator!");
		bpf_iter_scx_dsq_destroy(&it);
		return NULL;
	}

	struct task_struct *p = bpf_iter_scx_dsq_next(&it);
	bpf_iter_scx_dsq_destroy(&it);

	if (p)
		return p;

	ret = bpf_iter_scx_dsq_new(&it, ineligible_dsq(cpu), 0);
	if (unlikely(ret < 0)) {
		scx_bpf_error("Failed to allocate iterator!");
		bpf_iter_scx_dsq_destroy(&it);
		return NULL;
	}

	p = bpf_iter_scx_dsq_next(&it);
	bpf_iter_scx_dsq_destroy(&it);

	return p;
}

static __always_inline struct task_ctx *
pick_first_task_ctx(struct scx_rq_ctx *qctx)
{
	return try_lookup_task_ctx(pick_first_task(qctx));
}

/*
 * XXX: strictly: vd_i += N*r_i/w_i such that: vd_i > ve_i
 * this is probably good enough.
 */
static inline bool update_deadline(struct scx_rq_ctx *qctx,
				   struct task_ctx *tctx)
{
	if ((s64)(tctx->vruntime - tctx->deadline) < 0)
		return false;

	/*
	 * EEVDF: vd_i = ve_i + r_i / w_i
	 */
	tctx->deadline = tctx->vruntime + calc_delta_fair(SCX_SLICE_DFL, tctx);

	/*
     * The task has consumed its request, reschedule.
     */
	if (qctx->nr_running > 1)
		return true;

	return false;
}

static void update_min_vruntime(struct scx_rq_ctx *qctx)
{
	struct task_ctx *task = pick_first_task_ctx(qctx);
	struct task_ctx *curr = curr_task_ctx(qctx);
	u64 vruntime = qctx->min_vruntime;

	if (curr) {
		if (curr->on_rq)
			vruntime = curr->vruntime;
		else
			curr = NULL;
	}

	if (task) {
		if (!curr)
			vruntime = task->min_vruntime;
		else
			vruntime = min_vruntime(vruntime, task->min_vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
	qctx->min_vruntime = __update_min_vruntime(qctx, vruntime);
}

static bool update_curr(struct scx_rq_ctx *qctx)
{
	struct task_ctx *curr = curr_task_ctx(qctx);
	bool resched = false;
	s64 delta_exec;
	u64 now = scx_bpf_now();

	if (unlikely(!curr))
		return resched;

	delta_exec = now - curr->exec_start;
	if (unlikely(delta_exec <= 0))
		return resched;

	curr->slice -= min_u64(curr->slice, delta_exec);
	curr->exec_start = now;
	curr->sum_exec_runtime += delta_exec;
	curr->vruntime += calc_delta_fair(delta_exec, curr);
	resched = update_deadline(qctx, curr);
	update_min_vruntime(qctx);

	return resched;
}

static inline void place_task(struct scx_rq_ctx *qctx, struct task_ctx *tctx,
			      bool init)
{
	u64 vslice, vruntime = avg_vruntime(qctx);
	s64 lag = 0;

	tctx->slice = SCX_SLICE_DFL;
	vslice = calc_delta_fair(tctx->slice, tctx);

	// SCHED_FEAT(PLACE_LAG, true)
	/*
     * Due to how V is constructed as the weighted average of entities,
     * adding tasks with positive lag, or removing tasks with negative lag
     * will move 'time' backwards, this can screw around with the lag of
     * other tasks.
     *
     * EEVDF: placement strategy #1 / #2
     */
	if (qctx->nr_running) {
		struct task_ctx *curr = curr_task_ctx(qctx);
		u64 load;

		lag = tctx->vlag;

		/*
         * If we want to place a task and preserve lag, we have to
         * consider the effect of the new task on the weighted
         * average and compensate for this, otherwise lag can quickly
         * evaporate.
         *
         * Lag is defined as:
         *
         *   lag_i = S - s_i = w_i * (V - v_i)
         *
         * To avoid the 'w_i' term all over the place, we only track
         * the virtual lag:
         *
         *   vl_i = V - v_i <=> v_i = V - vl_i
         *
         * And we take V to be the weighted average of all v:
         *
         *   V = (\Sum w_j*v_j) / W
         *
         * Where W is: \Sum w_j
         *
         * Then, the weighted average after adding an task with lag
         * vl_i is given by:
         *
         *   V' = (\Sum w_j*v_j + w_i*v_i) / (W + w_i)
         *      = (W*V + w_i*(V - vl_i)) / (W + w_i)
         *      = (W*V + w_i*V - w_i*vl_i) / (W + w_i)
         *      = (V*(W + w_i) - w_i*l) / (W + w_i)
         *      = V - w_i*vl_i / (W + w_i)
         *
         * And the actual lag after adding an task with vl_i is:
         *
         *   vl'_i = V' - v_i
         *         = V - w_i*vl_i / (W + w_i) - (V - vl_i)
         *         = vl_i - w_i*vl_i / (W + w_i)
         *
         * Which is strictly less than vl_i. So in order to preserve lag
         * we should inflate the lag before placement such that the
         * effective lag after placement comes out right.
         *
         * As such, invert the above relation for vl'_i to get the vl_i
         * we need to use such that the lag after placement is the lag
         * we computed before dequeue.
         *
         *   vl'_i = vl_i - w_i*vl_i / (W + w_i)
         *         = ((W + w_i)*vl_i - w_i*vl_i) / (W + w_i)
         *
         *   (W + w_i)*vl'_i = (W + w_i)*vl_i - w_i*vl_i
         *                   = W*vl_i
         *
         *   vl_i = (W + w_i)*vl'_i / W
         */
		load = qctx->avg_load;
		if (curr && curr->on_rq)
			load += scale_load_down(curr->load.weight);

		lag *= load + scale_load_down(tctx->load.weight);
		if (!load)
			load = 1;
		lag = div_s64(lag, load);
	}

	tctx->vruntime = vruntime - lag;

	// SCHED_FEAT(PLACE_LAG, true)
	/*
     * When joining the competition; the existing tasks will be,
     * on average, halfway through their slice, as such start tasks
     * off with half a slice to ease into the competition.
     */
	if (init)
		vslice /= 2;

	/*
     * EEVDF: vd_i = ve_i + r_i/w_i
     */
	tctx->deadline = tctx->vruntime + vslice;
}

static inline void update_enq(struct scx_rq_ctx *qctx, struct task_ctx *tctx)
{
	update_load_add(&qctx->load, tctx->load.weight);
	avg_vruntime_add(qctx, tctx);

	tctx->min_vruntime = tctx->vruntime;
	tctx->on_rq = true;
}

static inline void update_deq(struct scx_rq_ctx *qctx, struct task_ctx *tctx)
{
	update_load_sub(&qctx->load, tctx->load.weight);

	struct task_ctx *curr = curr_task_ctx(qctx);
	if (tctx != curr) {
		avg_vruntime_sub(qctx, tctx);
	}

	update_min_vruntime(qctx);

	tctx->on_rq = false;
}

/* TODO: We could do work balance when selecting cpu.*/
s32 BPF_STRUCT_OPS(eevdf_select_cpu, struct task_struct *p, s32 prev_cpu,
		   u64 wake_flags)
{
	s32 cpu;
	if (p->nr_cpus_allowed == 1 ||
	    scx_bpf_test_and_clear_cpu_idle(prev_cpu))
		cpu = prev_cpu;
	else {
		cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
		if (cpu < 0)
			cpu = prev_cpu;
	}
	return cpu;
}

void BPF_STRUCT_OPS(eevdf_enqueue, struct task_struct *p, u64 enq_flags)
{
	s32 cpu = scx_bpf_task_cpu(p);
	struct rq *rq = scx_bpf_cpu_rq(cpu);
	if (unlikely(!rq))
		return;

	struct scx_rq_ctx *qctx = try_lookup_scx_rq_ctx(cpu);
	if (unlikely(!qctx))
		return;
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
	struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (unlikely(!uctx))
		return;

	tctx->cpu = cpu;

	update_curr(qctx);
	place_task(qctx, tctx, 0);
	update_enq(qctx, tctx);

	update_user_task_ctx(uctx, tctx);

	struct task_struct *curr = curr_task(qctx);
	if (curr && task_nice(p) < task_nice(curr)) {
        update_rq_cand(qctx, p);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL,
				   enq_flags | SCX_ENQ_PREEMPT);
		goto out;
	}

	if (task_eligible(qctx, tctx))
		scx_bpf_dsq_insert_vtime(p, eligible_dsq(cpu), SCX_SLICE_DFL,
					 tctx->deadline, enq_flags);
	else
		scx_bpf_dsq_insert_vtime(p, ineligible_dsq(cpu), SCX_SLICE_DFL,
					 tctx->vlag, enq_flags);

out:
	qctx->nr_running = rq->nr_running;
}

void BPF_STRUCT_OPS(eevdf_dequeue, struct task_struct *p, u64 deq_flags)
{
	struct rq *rq = scx_bpf_cpu_rq(scx_bpf_task_cpu(p));
	if (unlikely(!rq))
		return;
	struct scx_rq_ctx *qctx = try_lookup_scx_rq_ctx(scx_bpf_task_cpu(p));
	if (unlikely(!qctx))
		return;
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
	struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (unlikely(!uctx))
		return;

	update_curr(qctx);
	update_task_lag(qctx, tctx);
	update_deq(qctx, tctx);

	update_user_task_ctx(uctx, tctx);

	qctx->nr_running = rq->nr_running;
}

void BPF_STRUCT_OPS(eevdf_dispatch, s32 cpu, struct task_struct *prev)
{
	struct scx_rq_ctx *qctx = try_lookup_scx_rq_ctx(cpu);
	if (unlikely(!qctx))
		return;

	struct task_struct *p = pick_first_task(qctx);
	if (!p || p == prev)
		return;

	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;

    update_rq_cand(qctx, p);
	scx_bpf_dsq_move_to_local(p->scx.dsq->id);
}

void BPF_STRUCT_OPS(eevdf_tick, struct task_struct *p)
{
	bool resched = false;

	struct scx_rq_ctx *qctx = this_scx_rq_ctx();
	if (unlikely(!qctx))
		return;
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
	struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (unlikely(!uctx))
		return;

	resched = update_curr(qctx);

	u64 now = scx_bpf_now();
	if (unlikely(!now)) {
		scx_bpf_error("Invalid time!");
		return;
	}
	if (resched || !tctx->slice) {
		p->scx.slice = 0;
		// p->scx.core_sched_at = now;
	}

	update_user_task_ctx(uctx, tctx);
}

void BPF_STRUCT_OPS(eevdf_running, struct task_struct *p)
{
	struct scx_rq_ctx *qctx = this_scx_rq_ctx();
	if (unlikely(!qctx))
		return;
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
	struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (unlikely(!uctx))
		return;

	tctx->exec_start = scx_bpf_now();
	tctx->prev_sum_exec_runtime = tctx->sum_exec_runtime;
	tctx->sum_exec_runtime = 0;

	update_user_task_ctx(uctx, tctx);
}

void BPF_STRUCT_OPS(eevdf_stopping, struct task_struct *p, bool runnable)
{
	struct scx_rq_ctx *qctx = this_scx_rq_ctx();
	if (unlikely(!qctx))
		return;
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
	struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (unlikely(!uctx))
		return;

	update_curr(qctx);
	update_user_task_ctx(uctx, tctx);
}

void BPF_STRUCT_OPS(eevdf_enable, struct task_struct *p)
{
	struct task_ctx *tctx;
	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0,
				    BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (unlikely(!tctx))
		return;
}

void BPF_STRUCT_OPS(eevdf_set_weight, struct task_struct *p, u32 weight) {
    struct task_ctx *tctx;	
    tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;
    update_load_set(&tctx->load, weight);
    __update_inv_weight(&tctx->load);
}

void BPF_STRUCT_OPS(eevdf_cpu_release, s32 cpu,
		    struct scx_cpu_release_args *args)
{
	scx_bpf_reenqueue_local();
}

s32 BPF_STRUCT_OPS(eevdf_init_task, struct task_struct *p)
{
	struct task_ctx *tctx;
	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0,
				    BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (unlikely(!tctx))
		return -ENOMEM;

	tctx->cpu = scx_bpf_task_cpu(p);
	tctx->vpid = try_request_vpid();
	if (tctx->vpid < 0)
		return -EINVAL;

    struct user_task_ctx *uctx = try_lookup_user_task_tcx(tctx);
	if (likely(uctx)) {
		uctx->pid = p->pid;
		uctx->nice = task_nice(p);
		uctx->cpu = tctx->cpu;
		uctx->load.weight = tctx->load.weight;
		uctx->load.inv_weight = tctx->load.inv_weight;
		uctx->exec_start = 0;
		uctx->vruntime = 0;
		uctx->deadline = 0;
		uctx->slice = 0;
	}
	return 0;
}

void BPF_STRUCT_OPS(eevdf_exit_task, struct task_struct *p,
		    struct scx_exit_task_args *args)
{
	struct task_ctx *tctx = try_lookup_task_ctx(p);
	if (unlikely(!tctx))
		return;

	try_release_vpid(tctx->vpid);
}

/* 
 * We create dispatch queues for each priority class (niceness).
 * The first `NICE_WIDTH` queues are globle dispatch queues.
 */
s32 BPF_STRUCT_OPS_SLEEPABLE(eevdf_init)
{
	s32 ret;
	u64 dsq_id = 0;
	s32 node, cpu;
	for (node = 0; node < NUM_NUMA_NODES; node++) {
		for (cpu = 0; cpu < NUM_NODE_CPUS; cpu++) {
			/* Eligible DSQ */
			ret = scx_bpf_create_dsq(dsq_id, node);
			if (ret) {
				scx_bpf_error("Failed to create DSQ!");
				return -ENOMEM;
			}
			bpf_printk("Create dsq : %d\n", dsq_id);
			dsq_id++;

			/* Ineligible DSQ */
			ret = scx_bpf_create_dsq(dsq_id, node);
			if (ret) {
				scx_bpf_error("Failed to create DSQ!");
				return -ENOMEM;
			}
			bpf_printk("Create dsq : %d\n", dsq_id);
			dsq_id++;
		}
	}

	struct scx_rq_ctx *qctx;
	for (cpu = 0; cpu < NUM_POSSIBLE_CPUS; cpu++) {
		qctx = try_lookup_scx_rq_ctx(cpu);
		if (unlikely(!qctx))
			return -ENOENT;
		qctx->cpu = cpu;
        qctx->cand_nice = MAX_NICE + 1;
	}
	return 0;
}

void BPF_STRUCT_OPS(eevdf_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(eevdf_ops,
	       .select_cpu = (void *)eevdf_select_cpu, //
	       .enqueue = (void *)eevdf_enqueue, //
	       .dequeue = (void *)eevdf_dequeue, //
	       .dispatch = (void *)eevdf_dispatch, //
	       .dispatch_max_batch = 1, //
	       .tick = (void *)eevdf_tick, //
	       .running = (void *)eevdf_running, //
	       .stopping = (void *)eevdf_stopping, //
	       .enable = (void *)eevdf_enable, //
	       .set_weight = (void *)eevdf_set_weight, //
	       .cpu_release = (void *)eevdf_cpu_release, //
	       .init_task = (void *)eevdf_init_task, //
	       .exit_task = (void *)eevdf_exit_task, //
	       .init = (void *)eevdf_init, //
	       .exit = (void *)eevdf_exit, //
	       .name = "eevdf");