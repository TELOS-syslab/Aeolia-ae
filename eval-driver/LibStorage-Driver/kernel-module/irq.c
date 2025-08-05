
#include "asm-generic/rwonce.h"
#include "asm/current.h"
#include "common.h"
#include "libstorage_common.h"
#include "linux/blk-mq.h"
#include "linux/gfp_types.h"
#include "linux/irqflags.h"
#include "linux/irqreturn.h"
#include "linux/kernel.h"
#include "linux/pci.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/smp.h"
#include "linux/stddef.h"
#include "logger.h"
#include "linux/sched.h"
#include <linux/irq.h>

#include <linux/semaphore.h>

int upcount, downcount;
extern ls_nvme_dev_instance nvme_instances[];
void kern_ls_sched_out(void __user *__args)
{
	int ret;
	ls_device_args args = { 0 };
	ls_nvme_dev_instance *instance;
	ret = copy_from_user(&args, __args, sizeof(ls_device_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy device args from user");
		return;
	}

	instance = nvme_instances + args.instance_id;
	WRITE_ONCE(instance->in_waiting, 1);

	ls_nvme_qp *qp = find_qp_by_qid(instance->nvme, instance->qid);
	BUG_ON(qp == NULL);
	local_irq_disable();
	del_uinv();
	if (check_uirr() || READ_ONCE(current->thread.waiting_uintr)) {
		// LOG_INFO("UIRR DETECTED, return");
		WRITE_ONCE(instance->in_waiting, 0);
		local_irq_enable();
		WRITE_ONCE(current->thread.waiting_uintr, 0);
		return;
	}
	if (READ_ONCE(instance->in_waiting)) {
		might_sleep();
		set_current_state(TASK_INTERRUPTIBLE);
		local_irq_enable();
		schedule();
		// LOG_INFO("FUCK IN KERNEL %d", current->pid);
	}
	WRITE_ONCE(current->thread.waiting_uintr, 0);
}

static irqreturn_t ls_nvme_irq_kernel_handler(int irq, void *dev_instance)
{
	ls_nvme_dev_instance *instance = (ls_nvme_dev_instance *)dev_instance;
	ls_nvme_qp *qp = find_qp_by_qid(instance->nvme, instance->qid);
	BUG_ON(qp == NULL);

	if (instance == NULL) {
		LOG_ERROR("Handle Invalid Instance IRQ");
		return IRQ_HANDLED;
	}
	struct task_struct *task = instance->task;
	if (task == NULL) {
		LOG_ERROR("Handle Invalid Task IRQ : No Task with PID : %d",
			  instance->owner_pid);
		return IRQ_HANDLED;
	}
	WRITE_ONCE(instance->in_waiting, 0);
	int ret = wake_up_process(task);
	WRITE_ONCE(task->thread.waiting_uintr, 1);

	// if (!ret) {
	// 	LOG_WARN("WARN NOT YIELD INTERRUPT! %d", task->pid);
	// }

	if (task_nice(task) < task_nice(current)) {
		instance->num_uintr_kernel++;

		if (current->flags & PF_EXITING ||
		    current->flags & PF_SUPERPRIV) {
			LOG_DEBUG("Task %d is exiting", current->pid);
			return IRQ_HANDLED;
		}

		set_tsk_need_resched(current);
	}

	return IRQ_HANDLED;
}

static int ls_register_irq(ls_nvme_dev *nvme, ls_nvme_dev_instance *instance,
			   ls_device_args *args)
{
	int ret = 0;
	unsigned int flags = IRQF_SHARED;
	instance->irq_name = kzalloc(PATH_SIZE, GFP_KERNEL);
	if (!instance->irq_name) {
		LOG_ERROR("Failed to allocate irq name");
		ret = -ENOMEM;
		goto exit;
	}
	LOG_INFO("request irq virq: %d", instance->virq);
	sprintf(instance->irq_name, "nvme%duirq%d", nvme->id,
		instance->upid_idx);
	ret = request_irq(instance->virq, ls_nvme_irq_kernel_handler, flags,
			  instance->irq_name, instance);
	LOG_INFO("return type of request_irq: %d", ret);
	if (ret < 0) {
		LOG_ERROR("Failed to request irq err : %d, virq : %d %s", ret,
			  instance->virq, instance->irq_name);
		kfree(instance->irq_name);
		goto exit;
	}
	return ret;
exit:
	return ret;
}
int ls_nvme_register_uintr(ls_nvme_dev *nvme, ls_nvme_dev_instance *instance,
			   ls_device_args *args)
{
	int ret = 0;
	struct irq_data *irq_data = irq_get_irq_data(instance->virq);
	struct irq_chip *irq_chip = irq_data->chip;
	const struct cpumask *mask = cpumask_of(smp_processor_id());
	irq_chip->irq_set_affinity(irq_data, mask, true);
	set_cpus_allowed_ptr(current, mask);

	ret = do_uintr_register_irq_handler(args->uintr_handler,
					    instance->virq);
	if (ret == -1) {
		return -ENOTSUPP;
	}
	instance->upid_idx = ret;
	args->upid_idx = ret;
	instance->owner_pid = current->pid;
	instance->task = current;

	ret = 0;
	LOG_INFO(
		"Set instance irq, virq: %d, irq_vector: %d, upid_idx: %d, BaseAddr : %p, ActualAddr : %p cpu : %d",
		instance->virq, instance->irq_vector, instance->upid_idx,
		upid_page, (struct uintr_upid *)upid_page + ret,
		smp_processor_id());
	return ret;
}
int ls_nvme_set_irq(ls_nvme_dev *nvme, ls_nvme_dev_instance *instance,
		    ls_device_args *args)
{
	int ret = 0;

	uint32_t irq_vec = 0;
	irq_vec = find_first_zero_bit(nvme->vec_bmap, nvme->vec_bmap_size) + 1;
	if (irq_vec >= nvme->vec_bmap_size) {
		LOG_ERROR("Failed to find free irq vector");
		ret = -ENODEV;
		goto exit;
	}

	set_bit(irq_vec - 1, nvme->vec_bmap);
	instance->irq_vector = irq_vec;
	LOG_DEBUG("irq_vec : %d", irq_vec);
	instance->virq = pci_irq_vector(nvme->pdev, irq_vec);
	if (instance->virq < 0) {
		LOG_ERROR("Failed to get virq for %d", irq_vec);
		ret = -ENODEV;
		goto exit;
	}
	ret = ls_register_irq(nvme, instance, args);
	if (ret) {
		clear_bit(irq_vec, nvme->vec_bmap);
		goto exit;
	}
	return 0;
exit:
	return ret;
}
void ls_destroy_nvme_irq(ls_nvme_dev_instance *instance)
{
	if (instance->irq_vector) {
		free_irq(instance->virq, instance);
		clear_bit(instance->irq_vector, instance->nvme->vec_bmap);
		kfree(instance->irq_name);
	}
}
int ls_reg_uintr(void __user *__args)
{
	int ret = 0;
	ls_device_args args = { 0 };
	ls_nvme_dev *nvme;
	ls_nvme_dev_instance *dev_instance;
	ret = copy_from_user(&args, __args, sizeof(ls_device_args));
	dev_instance = nvme_instances + args.instance_id;
	if (ret < 0) {
		LOG_ERROR("Failed to copy device args from user");
		ret = -EFAULT;
		goto exit;
	}

	if (args.ioclass == IOPRIO_CLASS_RT) {
		// sched_set_fifo(current);
	}
	LOG_INFO("Created uintr for task %d", current->pid);

	nvme = nid_to_nvme(args.nid);

	ret = ls_nvme_register_uintr(nvme, dev_instance, &args);

	ret = copy_to_user(__args, &args, sizeof(ls_device_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy uintr args to user");
		ret = -EFAULT;
		goto exit;
	}
exit:
	return ret;
}