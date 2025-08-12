#include "asm/apic.h"
#include "asm/uintr.h"
#include "asm/fpu/api.h"
#include "asm/fpu/types.h"
#include "asm/msr-index.h"
#include "asm/msr.h"
#include "linux/bits.h"
#include "linux/export.h"
#include "linux/gfp_types.h"
#include "linux/irq.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/sched/task.h"
#include "linux/slab.h"
#include "linux/smp.h"
#include "linux/syscalls.h"
#include "linux/types.h"

#define pr_fmt(fmt) "uintr: " fmt

struct uintr_upid *upid_page;
EXPORT_SYMBOL(upid_page);
pid_t uintr_pid = 12345;
EXPORT_SYMBOL(uintr_pid);
int uintr_debug = 0;
EXPORT_SYMBOL(uintr_debug);
static atomic_t upid_alloc_cnt = ATOMIC_INIT(0);
static inline u32 cpu_to_ndst(int cpu)
{
	u32 apicid = (u32)apic->cpu_present_to_apicid(cpu);

	WARN_ON_ONCE(apicid == BAD_APICID);

	if (!x2apic_enabled())
		return (apicid << 8) & 0xFF00;

	return apicid;
}
static struct uintr_upid *alloc_upid(void)
{
	if (!upid_page)
		return NULL;
	int upid_idx = atomic_inc_return(&upid_alloc_cnt);
	pr_info("[alloc_upid]: page %p, cnt %d, upid : %p\n", upid_page,
		upid_idx, upid_page + upid_idx);
	return upid_page + upid_idx;
}
static struct uintr_upid_ctx *alloc_upid_ctx(void)
{
	struct uintr_upid_ctx *upid_ctx;
	struct uintr_upid *upid;
	upid_ctx = kzalloc(sizeof(*upid_ctx), GFP_KERNEL);
	if (!upid_ctx)
		return NULL;
	// upid = kzalloc(sizeof(*upid), GFP_KERNEL);
	upid = alloc_upid();
	if (!upid) {
		kfree(upid_ctx);
		return NULL;
	}
	upid_ctx->upid = upid;
	upid_ctx->task = get_task_struct(current);
	return upid_ctx;
}
u8 g_vis_uinv;
int do_uintr_register_handler(u64 handler, u8 uinv)
{
	struct uintr_upid *upid;
	struct uintr_upid_ctx *upid_ctx;
	struct task_struct *task = current;
	void *xstate;
	u64 msr_uintr_misc;
	u64 msr_uintr_stackadj;
	upid_ctx = task->thread.upid_ctx;
	if (upid_ctx)
		kfree(upid_ctx);
	upid_ctx = alloc_upid_ctx();
	if (!upid_ctx)
		return -ENOMEM;
	task->thread.upid_ctx = upid_ctx;

	xstate = start_update_xsave_msrs(XFEATURE_UINTR);
	upid = upid_ctx->upid;
	pr_info("cpu : %d, uinv : %d, upid : %p\n", smp_processor_id(), uinv,
		upid);
	current->thread.waiting_uintr = 0;
	current->thread.uintr_vec = uinv;
	upid->nc.nv = uinv;
	upid->nc.ndst = cpu_to_ndst(smp_processor_id());
	g_vis_uinv = uinv;
	xsave_wrmsrl(xstate, MSR_IA32_UINTR_HANDLER, handler);
	xsave_wrmsrl(xstate, MSR_IA32_UINTR_PD, (u64)upid);
	xsave_rdmsrl(xstate, MSR_IA32_UINTR_MISC, &msr_uintr_misc);
	msr_uintr_misc |= (u64)uinv << 32;
	xsave_wrmsrl(xstate, MSR_IA32_UINTR_MISC, msr_uintr_misc);
	msr_uintr_stackadj = 256;
	xsave_wrmsrl(xstate, MSR_IA32_UINTR_STACKADJUST, msr_uintr_stackadj);
	task->thread.upid_activated = 1;
	end_update_xsave_msrs();
	upid->puir = 0x1;

	// return is the idx of the upid in the page
	return upid - (struct uintr_upid *)upid_page;
}
EXPORT_SYMBOL(do_uintr_register_handler);

int do_uintr_register_irq_handler(u64 handler, u32 irq)
{
	pr_info("registering virq %d - uinv %d with handler %llx, now %d, dest %d\n",
		irq, get_vec_by_msi_irq(irq), handler, smp_processor_id(),
		get_msi_dest_by_irq(irq));
	uintr_pid = current->pid;
	return do_uintr_register_handler(handler, get_vec_by_msi_irq(irq));
}
EXPORT_SYMBOL(do_uintr_register_irq_handler);

void switch_uintr_return(void)
{
	u64 misc_msr, uirr, puir;
	if (!!!current->thread.upid_activated)
		return;
	WARN_ON_ONCE(test_thread_flag(TIF_NEED_FPU_LOAD));
	// pr_info("pid : %d, current : %p, upid_ctx : %p, upid : %p\n",
	// 	current->pid, current, current->thread.upid_ctx, upid);
	/* Modify only the relevant bits of the MISC MSR */
	// CAN: rd is for not change other bits, but now, other bits are not used
	rdmsrl(MSR_IA32_UINTR_MISC, misc_msr);
	if (!(misc_msr & GENMASK_ULL(39, 32))) {
		misc_msr |= (u64)current->thread.uintr_vec << 32;
		wrmsrl(MSR_IA32_UINTR_MISC, misc_msr);
	}
	if (current->thread.waiting_uintr) {
		// pr_info("Kernel UINTR Detected\n");
		// if (!current->thread.upid_ctx ||
		//     !current->thread.upid_ctx->upid) {
		// 	pr_err("upid_ctx is NULL\n");
		// 	return;
		// }
		current->thread.waiting_uintr = 0;
		// wrmsrl(MSR_IA32_UINTR_RR, 2);
		// WRITE_ONCE(current->thread.upid_ctx->upid->puir, 4);
		apic->send_IPI_self(current->thread.uintr_vec);
		// pr_info("Sending SELF IPI\n");
	}
	if (uintr_debug) {
		rdmsrl_safe(MSR_IA32_UINTR_RR, &uirr);
		rdmsrl(MSR_IA32_UINTR_MISC, misc_msr);
		pr_info("get puir %lx, uirr %lx, misc %lx", puir, uirr,
			misc_msr);
		pr_info("cpu %d, uirr %llx, upid->pir : %llx\n",
			smp_processor_id(), uirr,
			current->thread.upid_ctx->upid->puir);
	}
}

struct uintr_upid *init_upid_mem(void)
{
	upid_page = (struct uintr_upid *)__get_free_pages(
		GFP_KERNEL | __GFP_ZERO, 9);
	pr_info("upid_page : %p\n", upid_page);
	atomic_set(&upid_alloc_cnt, 0);
	if (!upid_page)
		pr_info("Failed to allocate memory for upid");
	return upid_page;
}
EXPORT_SYMBOL(init_upid_mem);

void free_upid_mem(void)
{
	if (upid_page)
		free_pages((unsigned long)upid_page, 9);
	upid_page = NULL;
}
EXPORT_SYMBOL(free_upid_mem);

void show_all_uintr_status(int cpu)
{
	// struct uintr_upid *upid = current->thread.upid_ctx->upid;
	u64 uirr, umisc;
	// rdmsrl(MSR_IA32_UINTR_RR, uirr);
	// pr_info("cpu %d, upid %p, uinv %d, puir %llx, uirr %llx\n", smp_processor_id(), upid, upid->nc.nv, upid->puir, uirr);

	rdmsrl_safe_on_cpu(cpu, MSR_IA32_UINTR_RR, &uirr);
	rdmsrl_safe_on_cpu(cpu, MSR_IA32_UINTR_MISC, &umisc);

	pr_info("cpu %d, uirr %llx, umisc %llx\n", cpu, uirr, umisc);
}
EXPORT_SYMBOL(show_all_uintr_status);
struct uitt_entry {
	u8 valid;
	u8 user_vec;
	u8 reserved[6];
	u64 target_upid_addr;
} __packed __aligned(16);

struct uitt_entry uitt[10];
static void __temp_set_uitt_uitte(u8 uinv)
{
	uitt[0].user_vec = 0x10;
	uitt[0].target_upid_addr = (u64)((struct uintr_upid *)upid_page + 1);
	uitt[0].valid = 1;
	struct uintr_upid *upid = (void *)uitt[0].target_upid_addr;
	void *xstate;
	u64 msr64;

	/* Maybe WARN_ON_FPU */
	if (uinv)
		upid->nc.nv = uinv;
	pr_info("set uitte 0 with upid %p, vec %d, upid info : %d %d %d %llx",
		upid, g_vis_uinv, upid->nc.status, upid->nc.nv, upid->nc.ndst,
		upid->puir);
	xstate = start_update_xsave_msrs(XFEATURE_UINTR);

	xsave_wrmsrl(xstate, MSR_IA32_UINTR_TT, (u64)uitt | 1);
	xsave_rdmsrl(xstate, MSR_IA32_UINTR_MISC, &msr64);
	msr64 &= GENMASK_ULL(63, 32);
	msr64 |= 5 - 1;
	xsave_wrmsrl(xstate, MSR_IA32_UINTR_MISC, msr64);

	end_update_xsave_msrs();
}
SYSCALL_DEFINE1(uintr_send_uinv_test, u8 __user, uinv)
{
	// pr_info("sending uinv on cpu %d, with uinv %d\n", smp_processor_id(),
	// 	uinv);
	// // send uinv to local apic
	// uint64_t uihandler, uimisc, upidaddr, uirr;
	// rdmsrl_safe(MSR_IA32_UINTR_HANDLER, &uihandler);
	// rdmsrl_safe(MSR_IA32_UINTR_MISC, &uimisc);
	// rdmsrl_safe(MSR_IA32_UINTR_PD, &upidaddr);
	// rdmsrl_safe(MSR_IA32_UINTR_RR, &uirr);
	// struct uintr_upid *upid = (struct uintr_upid *)upidaddr;
	// upid->nc.status = 1;
	// pr_info("uintr handler %llx, uintr misc %llx, upid_addr %p, puir : %lld, uirr : %lld\n",
	// 	uihandler, uimisc & GENMASK_ULL(39, 32), upid, upid->puir,
	// 	uirr);
	// if (uinv)
	// 	apic->send_IPI_self(uinv);
	// pr_info("sn : %d, puir : %lld, uirr : %lld\n", upid->nc.status & 1,
	// 	upid->puir, uirr);
	__temp_set_uitt_uitte(uinv);
	return 0;
}

u64 check_uirr(void)
{
	u64 uirr;
	rdmsrl_safe(MSR_IA32_UINTR_RR, &uirr);
	return uirr;
}
EXPORT_SYMBOL(check_uirr);
void del_uinv(void)
{
	u64 misc_msr;
	rdmsrl(MSR_IA32_UINTR_MISC, misc_msr);
	misc_msr &= ~GENMASK_ULL(39, 32);
	wrmsrl(MSR_IA32_UINTR_MISC, misc_msr);
}
EXPORT_SYMBOL(del_uinv);