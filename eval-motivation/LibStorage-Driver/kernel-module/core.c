#include "asm/current.h"
#include "common.h"
#include "libstorage_common.h"
#include "libstorage_ioctl.h"
#include "linux/atomic/atomic-arch-fallback.h"
#include "linux/atomic/atomic-instrumented.h"
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
#include "logger.h"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gfp.h>

#define LS_MIGRATE_WORK_INTERVAL 250
static struct workqueue_struct *ls_migrate_wq;
static struct delayed_work ls_migrate_dwork;

int ls_cdev_major;
atomic_t ls_cur_instance = ATOMIC_INIT(0);
// int ls_cur_instance;
struct cdev ls_cdev;
struct class *ls_class;
struct device *ls_device;
ls_nvme_dev nvme_devices[MAX_NVME_DEVICES];
ls_nvme_dev_instance nvme_instances[MAX_NVME_INSTANCE];
extern struct mutex qp_mutex;
// ******************** Helpers FUNCTIONS *********************
static int path_to_nid(const char *path)
{
	int i;
	LOG_INFO("WANT NVME : %s", path);
	for (i = 0; i < MAX_NVME_DEVICES; i++) {
		if (strcmp(path, nvme_devices[i].path) == 0) {
			return i;
		}
	}
	return -1;
}
// ******************** CONTROL FUNCTIONS *********************

static int ls_open_device(void __user *__args)
{
	int ret = 0, instance_id;
	ls_device_args args = { 0 };
	ls_nvme_dev *nvme;
	ls_nvme_dev_instance *dev_instance;
	ret = copy_from_user(&args, __args, sizeof(ls_device_args));
	if (atomic_read(&ls_cur_instance) >= MAX_NVME_INSTANCE) {
		atomic_set(&ls_cur_instance, 0);
	}
	instance_id = atomic_inc_return(&ls_cur_instance);
	LOG_INFO("Open dev instance : %d", instance_id);
	dev_instance = &nvme_instances[instance_id];
	dev_instance->instance_id = instance_id;
	args.instance_id = instance_id;
	if (ret < 0) {
		LOG_ERROR("Failed to copy device args from user");
		ret = -EFAULT;
		goto exit;
	}

	ret = path_to_nid(args.path);
	if (ret < 0) {
		LOG_ERROR("Failed to find device");
		ret = -ENODEV;
		goto exit;
	}

	nvme = nvme_devices + ret;
	args.nid = nvme->id;

	ret = ls_nvme_set_irq(nvme, dev_instance, &args);
	if (ret) {
		goto exit;
	}
	args.max_hw_sectors = nvme->dev->ctrl.max_hw_sectors;
	args.lba_shift = nvme->lba_shift;
	args.nsid = nvme->nsid;
	args.db_stride = nvme->dev->db_stride;
	dev_instance->nvme = nvme;
	dev_instance->status = 1;
	ls_map_nvme(dev_instance, args.instance_id);
	LOG_INFO("NVMe Instance : %d, virq : %d, owner %d", instance_id,
		 dev_instance->virq, dev_instance->owner_pid);

	ret = copy_to_user(__args, &args, sizeof(ls_device_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy device args to user");
		ret = -EFAULT;
		goto exit;
	}
exit:
	return ret;
}
static int ls_close_device(void __user *__args)
{
	int ret = 0;
	ls_device_args args = { 0 };
	ls_nvme_dev *nvme;
	ls_nvme_dev_instance *instance;
	ls_nvme_qp *qp, *tmp;

	ret = copy_from_user(&args, __args, sizeof(ls_device_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy device args from user");
		ret = -EFAULT;
		goto exit;
	}

	nvme = nvme_devices + args.nid;
	instance = nvme_instances + args.instance_id;
	if (!instance) {
		LOG_ERROR("Failed to find instance");
		ret = -ENODEV;
		goto exit;
	}
	// TODO: Qpairs should bind to instance rather than nvme
	list_for_each_entry_safe(qp, tmp, &nvme->user_qpairs, next_qp)
	{
		LOG_WARN("Close dev warn, still open qp %d", qp->qid);
		// TODO: Check whether qps are released
	}
	ls_destroy_nvme_irq(instance);
	ls_unmap_nvme(instance);
	instance->status = 0;
	LOG_INFO(
		"Successfully close device nvme%d for %d with pid %d, total irq num : %d, on cpu%d",
		args.nid, args.upid_idx, instance->owner_pid,
		instance->num_uintr_kernel, smp_processor_id());

exit:
	return ret;
}

// ******************** DEVICE FUNCTIONS *********************
static long ls_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	switch (cmd) {
	case LIBSTORAGE_OPEN_DEVICE:
		LOG_DEBUG("Open Device");
		ret = ls_open_device((void __user *)arg);
		break;
	case LIBSTORAGE_CLOSE_DEVICE:
		LOG_DEBUG("Close Device");
		ret = ls_close_device((void __user *)arg);
		break;
	case LIBSTORAGE_REG_UINTR:
		LOG_DEBUG("Register UINTR");
		ret = ls_reg_uintr((void __user *)arg);
		break;
	case LIBSTORAGE_CREATE_QP:
		ret = ls_create_qp((void __user *)arg);
		break;
	case LIBSTORAGE_DELETE_QP:
		LOG_DEBUG("Delete QP");
		ret = ls_delete_qp((void __user *)arg);
		break;
	case LIBSTORAGE_SET_PRIORITY:
		LOG_DEBUG("Set Priority");
		ret = ls_set_priority((void __user *)arg);
		break;
	case LIBSTORAGE_SCHED_YIELD:
		LOG_DEBUG("Sched Yield");
		kern_ls_sched_out((void __user *)arg);
		break;
	default:
		LOG_ERROR("Invalid IOCTL command");
		ret = -EINVAL;
		break;
	}
	return ret;
}
static int ls_open(struct inode *inode, struct file *file)
{
	return 0;
}
static int ls_release(struct inode *inode, struct file *file)
{
	return 0;
}
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = ls_open,
	.release = ls_release,
	.unlocked_ioctl = ls_ioctl,
};

// ******************** QPAIR FUNCTIONS *********************

// ********************* WQ FUNCTIONS ***********************
static void ls_migrate_delayed_work(struct work_struct *dwork)
{
	LOG_INFO("ls_migrate_dwork is running on CPU %d", task_cpu(current));
	// TODO: To find a eligible task and a target core, then do the migration.
	queue_delayed_work(ls_migrate_wq, &ls_migrate_dwork,
			   LS_MIGRATE_WORK_INTERVAL);
}

// ******************** INIT FUNCTIONS *********************
static int ls_init_uintr(void)
{
	int ret = 0;
	upid_page = init_upid_mem();
	if (!upid_page) {
		ret = -ENOMEM;
	}
	return ret;
}

static int ls_nvme_probe(void)
{
	int i = 0, ret = 0;
	struct nvme_dev *dev = NULL;
	struct pci_dev *pdev = NULL;
	struct nvme_ns *ns = NULL;
	ls_nvme_dev *nvme = NULL;
	ret = request_module("nvme");
	if (ret < 0) {
		LOG_ERROR("Failed to request nvme module");
		ret = -ENODEV;
		goto exit;
	}

	while ((pdev = pci_get_class(PCI_CLASS_STORAGE_EXPRESS, pdev))) {
		if (i >= MAX_NVME_DEVICES) {
			LOG_ERROR("Too many nvme devices");
			ret = -ENODEV;
			goto exit;
		}

		dev = pci_get_drvdata(pdev);
		if (!dev) {
			LOG_ERROR("Failed to get nvme device");
			ret = -ENODEV;
			goto exit;
		}

		nvme = nvme_devices + dev->ctrl.instance;
		nvme->dev = dev;
		nvme->pdev = pdev;
		nvme->id = dev->ctrl.instance;
		snprintf(nvme->path, PATH_SIZE, "/dev/nvme%d", nvme->id);

		INIT_LIST_HEAD(&nvme->user_qpairs);
		bitmap_set(nvme->qid_bitmap, 0, dev->ctrl.queue_count);

		INIT_LIST_HEAD(&nvme->user_dev_instance);

		// TODO: Support multiple namespaces
		list_for_each_entry(ns, &dev->ctrl.namespaces, list)
		{
			nvme->lba_shift = ns->head->lba_shift;
			nvme->nsid = ns->head->ns_id;
			break;
		}

		if (pdev->msix_enabled) {
			nvme->msix_entry = NULL;
			nvme->irq_vec_max = dev->max_qid;
			nvme->vec_bmap_size = pci_msix_vec_count(pdev);
			bitmap_set(nvme->vec_bmap, 0, dev->max_qid);
		}

		i++;

		for (int j = 0; j < nvme->vec_bmap_size; j++) {
			LOG_INFO(" dev_vec : %d, virq : %d", j,
				 pci_irq_vector(pdev, j));
		}
		// TODO: Get HW Queue Limit from NVMe
		// ret = nvme_get_queue_count(nvme);
		// if (ret < 0) {
		// 	LOG_ERROR("Failed to get queue count");
		// 	continue;
		// }
		nvme->num_hardware_qp_limit = 128;
		// LOG ALL INFO
		LOG_INFO(
			"NVMe Device %d: %s, NSID: %d, LBA Shift: %d, HW QP Limit: %d, Vec Count: %d, Max_qid : %d",
			nvme->id, nvme->path, nvme->nsid, nvme->lba_shift,
			nvme->num_hardware_qp_limit, nvme->vec_bmap_size,
			nvme->irq_vec_max);
	}
exit:
	return ret;
}
static void ls_nvme_destroy(void)
{
	int i, j;
	ls_nvme_dev *nvme;
	ls_nvme_qp *qp, *tmp;
	ls_nvme_dev_instance *instance;
	for (i = 0; i < MAX_NVME_DEVICES; i++) {
		nvme = nvme_devices + i;
		if (!nvme->dev)
			continue;

		// TODO: QPs should belong to instance rather than nvme.
		list_for_each_entry_safe(qp, tmp, &nvme->user_qpairs, next_qp)
		{
			LOG_INFO("Clean QP %d", qp->qid);
			ls_do_qp_release(nvme, qp);
		}
	}

	int max_instance_cur = atomic_read(&ls_cur_instance);
	LOG_INFO("max_instance_cur : %d, status : %d", max_instance_cur,
		 nvme_instances[1].status);
	for (j = 1; j <= max_instance_cur; j++) {
		instance = nvme_instances + j;
		if (instance->status != 1) {
			continue;
		}
		LOG_INFO("Clean Instance %d", j);
		ls_destroy_nvme_irq(instance);
		ls_unmap_nvme(instance);
		instance->status = 0;
	}
}
static int ls_init_cdev(void)
{
	int ret = 0;
	ls_cdev_major = register_chrdev(0, DEVICE_NAME, &fops);
	if (ls_cdev_major < 0) {
		LOG_ERROR("Failed to register char device");
		ret = ls_cdev_major;
		goto exit;
	}

	ls_class = class_create(CLASS_NAME);
	if (IS_ERR(ls_class)) {
		LOG_ERROR("Failed to create class");
		ret = PTR_ERR(ls_class);
		goto failed_class;
	}

	ls_device = device_create(ls_class, NULL, MKDEV(ls_cdev_major, 0), NULL,
				  DEVICE_NAME);
	if (IS_ERR(ls_device)) {
		LOG_ERROR("Failed to create device");
		ret = PTR_ERR(ls_device);
		goto failed_device;
	}

	cdev_init(&ls_cdev, &fops);
	ls_cdev.owner = THIS_MODULE;
	ret = cdev_add(&ls_cdev, MKDEV(ls_cdev_major, 0), 1);
	if (ret < 0) {
		LOG_ERROR("Failed to add cdev");
		goto failed_cdev;
	}

	LOG_INFO("LibStorage char device registered");
	return ret;
failed_cdev:
	device_destroy(ls_class, MKDEV(ls_cdev_major, 0));
failed_device:
	class_destroy(ls_class);
failed_class:
	unregister_chrdev(ls_cdev_major, DEVICE_NAME);
exit:
	return ret;
}

static void ls_destroy_cdev(void)
{
	LOG_INFO("exit... destroy cdev");
	ls_nvme_destroy();
	cdev_del(&ls_cdev);
	device_destroy(ls_class, MKDEV(ls_cdev_major, 0));
	class_unregister(ls_class);
	class_destroy(ls_class);
	unregister_chrdev(ls_cdev_major, DEVICE_NAME);
	LOG_INFO("exit... rm chrdev " DEVICE_NAME);
}
static void ls_revert_nvme(void)
{
	//TODO: If irq make kernel boom, revert nvme;
}
static int __init ls_init(void)
{
	LOG_DEBUG("LibStorage Module Init ...");
	int ret = 0;

	ret = ls_init_cdev();
	if (ret < 0) {
		goto exit;
	}

	mutex_init(&qp_mutex);
	ret = ls_nvme_probe();
	if (ret < 0) {
		goto exit;
	}

	ret = ls_init_uintr();
	if (ret < 0) {
		goto failed_uintr;
	}

	ls_migrate_wq =
		alloc_workqueue("ls_migrate_wq", WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!ls_migrate_wq) {
		LOG_ERROR("Failed to create ls_migrate_wq");
		goto exit;
	}
	// INIT_DELAYED_WORK(&ls_migrate_dwork, ls_migrate_delayed_work);
	// queue_delayed_work(ls_migrate_wq, &ls_migrate_dwork,
	// 		   LS_MIGRATE_WORK_INTERVAL);

	return ret;

failed_cdev:
	free_upid_mem();
failed_uintr:
	ls_revert_nvme();
exit:
	return ret;
}

static void __exit ls_exit(void)
{
	LOG_ERROR("LibStorage Module Exit ...");
	ls_destroy_cdev();
	LOG_ERROR("Cdev Destroyed ...");
	free_upid_mem();
	LOG_ERROR("Uintr Destroyed ...");
	ls_revert_nvme();
	cancel_delayed_work_sync(&ls_migrate_dwork);
	destroy_workqueue(ls_migrate_wq);
	LOG_ERROR("Workqueue Destroyed ...");
	mutex_destroy(&qp_mutex);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ran Yi");

module_init(ls_init);
module_exit(ls_exit);