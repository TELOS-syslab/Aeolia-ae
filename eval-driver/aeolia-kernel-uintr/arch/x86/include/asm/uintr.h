/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UINTR_H
#define _ASM_X86_UINTR_H
/* TODO: Separate out the hardware definitions from the software ones */
/* User Posted Interrupt Descriptor (UPID) */
#include <linux/sched.h>
extern pid_t uintr_pid;
static inline bool is_uintr_task(struct task_struct *task)
{
	return task->thread.upid_activated;
}
struct uintr_upid {
	struct {
		u8 status; /* bit 0: ON, bit 1: SN, bit 2-7: reserved */
		u8 reserved1; /* Reserved */
		u8 nv; /* Notification vector */
		u8 reserved2; /* Reserved */
		u32 ndst; /* Notification destination */
	} nc __packed; /* Notification control */
	volatile u64 puir; /* Posted user interrupt requests */
} __aligned(64);
/* UPID Notification control status bits */
#define UINTR_UPID_STATUS_ON 0x0 /* Outstanding notification */
#define UINTR_UPID_STATUS_SN 0x1 /* Suppressed notification */

int do_uintr_register_handler(u64 handler, u8 uinv);
struct uintr_upid_ctx {
	struct list_head node;
	struct task_struct *task; /* Receiver task */
	u64 uvec_mask; /* track registered vectors per bit */
	struct uintr_upid *upid;
	/* TODO: Change to kernel kref api */
	refcount_t refs;
};
void switch_uintr_return(void);
int do_uintr_register_irq_handler(u64 handler, u32 irq);
struct uintr_upid *init_upid_mem(void);
void free_upid_mem(void);
void show_all_uintr_status(int cpu);
u64 check_uirr(void);
void del_uinv(void);
#endif /* _ASM_X86_UINTR_H */