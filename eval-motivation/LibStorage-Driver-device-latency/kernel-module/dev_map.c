#include "linux/errno.h"
#include "common.h"
#include "libstorage_ioctl.h"
#include "linux/cdev.h"
#include "linux/fs.h"
#include "logger.h"
// MKDEV-0 : Major Dev, [1, 2, 3, 4] : qid_cq, qid_sq, qid_db, qid_upid
void ls_delete_qp_mapping(ls_nvme_qp *qp)
{
	cdev_del(&qp->cq_cdev);
	cdev_del(&qp->sq_cdev);
	cdev_del(&qp->db_cdev);

	int rcq = refcount_read(&qp->cq_cdev.kobj.kref.refcount);
	int rsq = refcount_read(&qp->sq_cdev.kobj.kref.refcount);
	int rdb = refcount_read(&qp->db_cdev.kobj.kref.refcount);
	LOG_INFO("Cdev Del for qid %d, rcq : %d, rsq : %d, rdb : %d", qp->qid,
		 rcq, rsq, rdb);
}
void ls_destroy_mmap_files(int qid)
{
	device_destroy(ls_class, MKDEV(ls_cdev_major, qid * 4 + 1));
	device_destroy(ls_class, MKDEV(ls_cdev_major, qid * 4 + 2));
	device_destroy(ls_class, MKDEV(ls_cdev_major, qid * 4 + 3));
	LOG_INFO("dev destroy for qid %d", qid);
}
static int create_mmap_files(ls_nvme_dev *nvme, int qid)
{
	char cq_name[PATH_SIZE], sq_name[PATH_SIZE], db_name[PATH_SIZE];
	struct device *cq_dev, *sq_dev, *db_dev;
	int ret = 0;
	snprintf(cq_name, PATH_SIZE, LIBSTORAGE_CQ_PATH_FORMAT, nvme->id, qid);
	snprintf(sq_name, PATH_SIZE, LIBSTORAGE_SQ_PATH_FORMAT, nvme->id, qid);
	snprintf(db_name, PATH_SIZE, LIBSTORAGE_DB_PATH_FORMAT, nvme->id, qid);

	cq_dev = device_create(ls_class, NULL,
			       MKDEV(ls_cdev_major, qid * 4 + 1), NULL,
			       cq_name);
	if (IS_ERR(cq_dev)) {
		LOG_WARN("Failed to create device for %s", cq_name);
		ret = PTR_ERR(cq_dev);
		goto exit;
	}

	sq_dev = device_create(ls_class, NULL,
			       MKDEV(ls_cdev_major, qid * 4 + 2), NULL,
			       sq_name);
	if (IS_ERR(sq_dev)) {
		LOG_WARN("Failed to create device for %s", sq_name);
		ret = PTR_ERR(sq_dev);
		goto destroy_cq;
	}

	db_dev = device_create(ls_class, NULL,
			       MKDEV(ls_cdev_major, qid * 4 + 3), NULL,
			       db_name);
	if (IS_ERR(db_dev)) {
		LOG_WARN("Failed to create device for %s", db_name);
		ret = PTR_ERR(db_dev);
		goto destroy_sq;
	}

	LOG_INFO("Successfully Create mmap files, cq %s_%d, sq %s_%d, db %s_%d",
		 cq_name, qid * 4 + 1, sq_name, qid * 4 + 2, db_name,
		 qid * 4 + 3);
	return ret;
destroy_sq:
	device_destroy(ls_class, MKDEV(ls_cdev_major, qid * 4 + 2));
destroy_cq:
	device_destroy(ls_class, MKDEV(ls_cdev_major, qid * 4 + 1));
exit:
	return ret;
}
static int cq_open(struct inode *inode, struct file *file)
{
	ls_nvme_qp *qp = container_of(inode->i_cdev, ls_nvme_qp, cq_cdev);
	if (qp->owner_pid != current->pid) {
		LOG_WARN("CQ open failed, owner mismatch\n");
		return -EPERM;
	}
	file->private_data = qp;
	return 0;
}
static int sq_open(struct inode *inode, struct file *file)
{
	ls_nvme_qp *qp = container_of(inode->i_cdev, ls_nvme_qp, sq_cdev);
	if (qp->owner_pid != current->pid) {
		LOG_WARN("SQ open failed, owner mismatch\n");
		return -EPERM;
	}
	file->private_data = qp;
	return 0;
}

static int db_open(struct inode *inode, struct file *file)
{
	ls_nvme_qp *qp = container_of(inode->i_cdev, ls_nvme_qp, db_cdev);
	if (qp->owner_pid != current->pid) {
		LOG_WARN("DB open failed, owner mismatch\n");
		return -EPERM;
	}
	file->private_data = qp;
	return 0;
}

static int upid_open(struct inode *inode, struct file *file)
{
	ls_nvme_dev_instance *instance =
		container_of(inode->i_cdev, ls_nvme_dev_instance, upid_cdev);
	// if (instance->owner_pid != current->pid) {
	// 	LOG_WARN("UPID open failed, owner mismatch\n");
	// 	return -EPERM;
	// }
	file->private_data = instance;
	return 0;
}
static int cq_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ls_nvme_qp *qp = filp->private_data;
	u64 map_size;
	map_size = qp->q_depth * sizeof(struct nvme_completion);
	// vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if ((vma->vm_end - vma->vm_start) < map_size) {
		LOG_WARN("mmap_cq failed, size too small\n");
		return -EINVAL;
	}
	LOG_INFO("mmap cq addr : %p", qp->cqes);
	return dma_mmap_attrs(&qp->nvme_dev->pdev->dev, vma, qp->cqes,
			      qp->cqes_dma, map_size, 0);
}

static int sq_mmap(struct file *filp, struct vm_area_struct *vma)
{
	ls_nvme_qp *qp = filp->private_data;
	u64 map_size;
	map_size = qp->q_depth * sizeof(struct nvme_command);
	// vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	LOG_INFO("mmap_sq addr : %p", qp->sqes);
	if ((vma->vm_end - vma->vm_start) < map_size) {
		return -EINVAL;
	}
	return dma_mmap_attrs(&qp->nvme_dev->pdev->dev, vma, qp->sqes,
			      qp->sqes_dma, map_size, 0);
}

// FIXME: The db space is hardcode to use 2 pages.
static int db_mmap(struct file *filp, struct vm_area_struct *vma)
{
	ls_nvme_qp *qp = filp->private_data;
	u64 map_size;
	map_size = 2 * PAGE_SIZE;
	// vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	LOG_INFO("mmap_db addr : %p map_size : %llu", qp->nvme_dev->dev->dbs,
		 map_size);
	return io_remap_pfn_range(vma, vma->vm_start,
				  pci_resource_start(qp->nvme_dev->pdev, 0) >>
					  PAGE_SHIFT,
				  map_size, vma->vm_page_prot);
}

static int upid_mmap(struct file *filp, struct vm_area_struct *vma)
{
	LOG_INFO("mmap upid for task %d pfn : %p", current->pid,
		 virt_to_phys(upid_page) >> PAGE_SHIFT);
	u64 map_size;
	map_size = PAGE_SIZE;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return io_remap_pfn_range(vma, vma->vm_start,
				  virt_to_phys(upid_page) >> PAGE_SHIFT,
				  map_size, vma->vm_page_prot);
}

static struct file_operations cq_fops = {
	.owner = THIS_MODULE,
	.open = cq_open,
	.mmap = cq_mmap,
};

static struct file_operations sq_fops = {
	.owner = THIS_MODULE,
	.mmap = sq_mmap,
	.open = sq_open,
};

static struct file_operations db_fops = {
	.owner = THIS_MODULE,
	.mmap = db_mmap,
	.open = db_open,
};

static struct file_operations upid_fops = {
	.owner = THIS_MODULE,
	.mmap = upid_mmap,
	.open = upid_open,
};
int ls_map_nvme(ls_nvme_dev_instance *instance, int instance_id)
{
	char upid_name[PATH_SIZE];
	int ret = 0;
	struct device *upid_dev;

	snprintf(upid_name, PATH_SIZE, LIBSTORAGE_UPID_PATH_FORMAT,
		 instance->nvme->id, instance_id);
	upid_dev = device_create(ls_class, NULL,
				 MKDEV(ls_cdev_major, instance_id * 4 + 4),
				 NULL, upid_name);
	if (IS_ERR(upid_dev)) {
		LOG_WARN("Failed to create device for %s", upid_name);
		ret = PTR_ERR(upid_dev);
		return ret;
	}

	cdev_init(&instance->upid_cdev, &upid_fops);
	instance->upid_cdev.owner = THIS_MODULE;
	ret = cdev_add(&instance->upid_cdev,
		       MKDEV(ls_cdev_major, instance_id * 4 + 4), 1);

	LOG_INFO("Init UPID dev, dev_path : %s, dev id : %d", upid_name,
		 instance_id * 4 + 4);
	if (ret < 0) {
		LOG_ERROR("Failed to add cdev for upid");
		device_destroy(ls_class,
			       MKDEV(ls_cdev_major, instance_id * 4 + 4));
		return ret;
	}
	return ret;
}
void ls_unmap_nvme(ls_nvme_dev_instance *instance)
{
	LOG_DEBUG("start destroy upid dev");
	cdev_del(&instance->upid_cdev);
	device_destroy(ls_class,
		       MKDEV(ls_cdev_major, instance->instance_id * 4 + 4));
	LOG_DEBUG("unmap instance : %d", instance->instance_id);
}
int ls_map_qp(ls_nvme_dev *nvme, int qid, ls_nvme_qp *qp)
{
	int ret = 0;

	ret = create_mmap_files(nvme, qid);
	if (ret < 0) {
		LOG_ERROR("Failed to create mmap files");
		goto exit;
	}

	cdev_init(&qp->cq_cdev, &cq_fops);
	qp->cq_cdev.owner = THIS_MODULE;
	ret = cdev_add(&qp->cq_cdev, MKDEV(ls_cdev_major, qid * 4 + 1), 1);
	if (ret < 0) {
		LOG_ERROR("Failed to add cdev for cq");
		goto error;
	}
	LOG_INFO("Add cdev for cq");
	cdev_init(&qp->sq_cdev, &sq_fops);
	qp->sq_cdev.owner = THIS_MODULE;
	ret = cdev_add(&qp->sq_cdev, MKDEV(ls_cdev_major, qid * 4 + 2), 1);
	if (ret < 0) {
		LOG_ERROR("Failed to add cdev for sq");
		goto error;
	}

	LOG_INFO("Add cdev for sq");
	cdev_init(&qp->db_cdev, &db_fops);
	qp->db_cdev.owner = THIS_MODULE;
	ret = cdev_add(&qp->db_cdev, MKDEV(ls_cdev_major, qid * 4 + 3), 1);
	if (ret < 0) {
		LOG_ERROR("Failed to add cdev for db");
		goto error;
	}
	LOG_INFO("Add cdev for db");

exit:
	return ret;
error:
	ls_destroy_mmap_files(qid);
	return -EFAULT;
}