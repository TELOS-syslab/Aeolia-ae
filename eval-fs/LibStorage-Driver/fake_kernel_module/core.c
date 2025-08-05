#include "asm/current.h"
#include "linux/bitmap-str.h"
#include "linux/bitmap.h"
#include "linux/device.h"
#include "linux/dma-mapping.h"
#include "linux/fs.h"
#include "linux/kmod.h"
#include "linux/mutex.h"
#include "linux/mutex_types.h"
#include "linux/nvme.h"
#include "linux/pci.h"
#include "linux/sched.h"
#include "linux/smp.h"
#include "linux/stddef.h"
#include "linux/workqueue.h"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gfp.h>

static int __init aa_init(void)
{
	pid_t pid = 9920;
	struct task_struct* task = find_task_by_vpid(pid);
	int ret = wake_up_process(task);
	pr_info("ret is %d", ret);
	return 0;
}

static void __exit aa_exit(void)
{
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ran Yi");

module_init(aa_init);
module_exit(aa_exit);