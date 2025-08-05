#include "common.h"
#include <linux/mutex.h>
#include "linux/nvme.h"
#include "linux/uaccess.h"
#include "logger.h"
#include "libstorage_common.h"

struct mutex qp_mutex;
ls_nvme_qp *find_qp_by_qid(struct ls_nvme_dev *nvme, int qid)
{
	ls_nvme_qp *qp_info;
	list_for_each_entry(qp_info, &nvme->user_qpairs, next_qp)
	{
		if (qp_info->qid == qid) {
			return qp_info;
		}
	}
	return NULL;
}
// ******************** QPAIR FUNCTIONS *********************
static int adapter_alloc_sq(ls_nvme_dev *nvme, uint16_t qid, ls_nvme_qp *qp)
{
	int ret = 0;

	struct nvme_ctrl *ctrl = &nvme->dev->ctrl;
	struct nvme_command cmd = {};

	int flags = NVME_QUEUE_PHYS_CONTIG;

	if (ctrl->quirks & NVME_QUIRK_MEDIUM_PRIO_SQ)
		flags |= NVME_SQ_PRIO_MEDIUM;

	if (qp->flags == NVME_SQ_PRIO_URGENT)
		flags |= NVME_SQ_PRIO_URGENT;
	else if (qp->flags == NVME_SQ_PRIO_HIGH)
		flags |= NVME_SQ_PRIO_HIGH;
	else if (qp->flags == NVME_SQ_PRIO_MEDIUM)
		flags |= NVME_SQ_PRIO_MEDIUM;
	else if (qp->flags == NVME_SQ_PRIO_LOW)
		flags |= NVME_SQ_PRIO_LOW;

	cmd.create_sq.opcode = nvme_admin_create_sq;
	cmd.create_sq.prp1 = cpu_to_le64(qp->sqes_dma);
	cmd.create_sq.sqid = cpu_to_le16(qid);
	cmd.create_sq.qsize = cpu_to_le16(qp->q_depth - 1);
	cmd.create_sq.sq_flags = cpu_to_le16(flags);
	cmd.create_sq.cqid = cpu_to_le16(qid);

	LOG_INFO(
		"Control Config : %x Create SQ for %d FLAG %x ||||| QP FLAGS : %x",
		ctrl->ctrl_config, qid, flags, qp->flags);

	ret = nvme_submit_sync_cmd(nvme->dev->ctrl.admin_q, &cmd, NULL, 0);
	return ret;
}
static int adapter_alloc_cq(ls_nvme_dev *nvme, uint16_t qid, ls_nvme_qp *qp)
{
	int ret = 0;

	struct nvme_command cmd = {};

	int flags = NVME_QUEUE_PHYS_CONTIG;

	if (qp->interrupt == LS_INTERRPUT_UINTR) {
		flags |= NVME_CQ_IRQ_ENABLED;
		cmd.create_cq.irq_vector =
			cpu_to_le16(qp->instance->irq_vector);
	}

	cmd.create_cq.opcode = nvme_admin_create_cq;
	cmd.create_cq.prp1 = cpu_to_le64(qp->cqes_dma);
	cmd.create_cq.cqid = cpu_to_le16(qid);
	cmd.create_cq.qsize = cpu_to_le16(qp->q_depth - 1);
	cmd.create_cq.cq_flags = cpu_to_le16(flags);

	ret = nvme_submit_sync_cmd(nvme->dev->ctrl.admin_q, &cmd, NULL, 0);
	return ret;
}
static int adapter_delete_queue(struct ls_nvme_dev *nvme, uint8_t opcode,
				u16 id)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	return nvme_submit_sync_cmd(nvme->dev->ctrl.admin_q, &c, NULL, 0);
}
static int ls_init_qp(ls_nvme_dev *nvme, int qid, uint16_t depth,
		      ls_nvme_qp *qp, int flags)
{
	int ret = 0;

	qp->sqes = dma_alloc_coherent(&nvme->pdev->dev,
				      depth * sizeof(struct nvme_command),
				      &qp->sqes_dma, GFP_KERNEL);
	if (!qp->sqes) {
		LOG_ERROR("Failed to allocate sqes");
		ret = -ENOMEM;
		goto exit;
	}
	qp->cqes = dma_alloc_coherent(&nvme->pdev->dev,
				      depth * sizeof(struct nvme_completion),
				      &qp->cqes_dma, GFP_KERNEL);
	if (!qp->cqes) {
		LOG_ERROR("Failed to allocate cqes");
		ret = -ENOMEM;
		goto free_sqes;
	}
	LOG_INFO("Successfully alloc qp entries");
	INIT_LIST_HEAD(&qp->next_qp);
	qp->nvme_dev = nvme;
	qp->q_depth = depth;
	qp->qid = qid;
	qp->q_db = &nvme->dev->dbs[qid * 2 * nvme->dev->db_stride];
	qp->owner_pid = current->pid;
	qp->flags = flags;
	ret = adapter_alloc_cq(nvme, qid, qp);
	if (ret < 0) {
		LOG_ERROR("Failed to allocate cq");
		goto free_cqes;
	}
	LOG_DEBUG("Alloc CQ for %d Success", qid);
	ret = adapter_alloc_sq(nvme, qid, qp);
	if (ret < 0) {
		LOG_ERROR("Failed to allocate sq");
		goto free_cqes;
	}

	LOG_DEBUG("Alloc SQ for %d Success", qid);
	memset((void *)qp->cqes, 0, depth * sizeof(struct nvme_completion));
	mutex_lock(&qp_mutex);
	list_add_tail(&qp->next_qp, &nvme->user_qpairs);
	mutex_unlock(&qp_mutex);
	LOG_INFO("Successfully alloc qp from adapter");

	return ret;
free_cqes:
	dma_free_coherent(&nvme->pdev->dev,
			  depth * sizeof(struct nvme_completion), qp->cqes,
			  qp->cqes_dma);
free_sqes:
	dma_free_coherent(&nvme->pdev->dev, depth * sizeof(struct nvme_command),
			  qp->sqes, qp->sqes_dma);
exit:
	return ret;
}

int ls_create_qp(void __user *__args)
{
	int ret = 0;
	ls_queue_args args = { 0 };
	ls_nvme_dev *nvme;
	struct nvme_dev *dev;
	ls_nvme_qp *qp;
	ls_nvme_dev_instance *instance;
	int qid;

	ret = copy_from_user(&args, __args, sizeof(ls_queue_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy queue args from user");
		ret = -EFAULT;
		goto exit;
	}
	nvme = nid_to_nvme(args.nid);
	instance = nvme_instances + args.instance_id;
	dev = nvme->dev;

	if (nvme->num_user_qpairs + dev->ctrl.queue_count >
	    nvme->num_hardware_qp_limit) {
		LOG_ERROR("Too many user qpairs");
		ret = -ENODEV;
		goto exit;
	}

	qid = find_first_zero_bit(nvme->qid_bitmap, MAX_NVME_QPAIRS);
	instance->qid = qid;
	set_bit(qid, nvme->qid_bitmap);
	LOG_INFO("Creating QPair for pid : %d, with , nvme_id : %d, qp_id : %d",
		 current->pid, args.nid, qid);

	qp = kzalloc(sizeof(ls_nvme_qp), GFP_KERNEL);
	if (!qp) {
		LOG_ERROR("Failed to allocate qp");
		ret = -ENOMEM;
		goto exit;
	}
	qp->interrupt = args.interrupt;
	qp->instance = nvme_instances + args.instance_id;
	if (!qp->instance) {
		LOG_ERROR("Failed to find instance for pid : %d, idx : %d",
			  current->pid, args.upid_idx);
		return -ENODEV;
	}
	ret = ls_init_qp(nvme, qid, args.depth, qp, args.flags);
	if (ret < 0) {
		LOG_ERROR("Failed to init qp");
		goto free_qp;
	}

	ret = ls_map_qp(nvme, qid, qp);
	if (ret < 0) {
		LOG_ERROR("Failed to map qp");
		goto destroy_qp;
	}
	nvme->num_user_qpairs++;
	args.qid = qid;
	// uint32_t arb = 0;

	ret = copy_to_user(__args, &args, sizeof(ls_queue_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy queue args to user");
		ret = -EFAULT;
		goto free_qp;
	}

	return ret;
destroy_qp:
	// TODO: Destroy QP
free_qp:
	kfree(qp);
exit:
	return ret;
}
int ls_do_qp_release(ls_nvme_dev *nvme, ls_nvme_qp *qp)
{
	LOG_INFO("release qp %d", qp->qid);
	adapter_delete_queue(nvme, nvme_admin_delete_sq, qp->qid);
	adapter_delete_queue(nvme, nvme_admin_delete_cq, qp->qid);
	mutex_lock(&qp_mutex);
	list_del(&qp->next_qp);
	mutex_unlock(&qp_mutex);
	dma_free_coherent(&nvme->pdev->dev,
			  qp->q_depth * sizeof(struct nvme_completion),
			  qp->cqes, qp->cqes_dma);
	dma_free_coherent(&nvme->pdev->dev,
			  qp->q_depth * sizeof(struct nvme_command), qp->sqes,
			  qp->sqes_dma);

	ls_delete_qp_mapping(qp);
	ls_destroy_mmap_files(qp->qid);

	clear_bit(qp->qid, nvme->qid_bitmap);
	return 0;
}
int ls_delete_qp(void __user *__args)
{
	int ret = 0;
	ls_queue_args args = { 0 };
	ls_nvme_dev *nvme;
	ls_nvme_qp *qp;
	ret = copy_from_user(&args, __args, sizeof(ls_queue_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy queue args from user");
		ret = -EFAULT;
		goto exit;
	}
	nvme = nid_to_nvme(args.nid);
	qp = find_qp_by_qid(nvme, args.qid);
	if (!qp) {
		LOG_ERROR("Failed to find qp");
		ret = -ENODEV;
		goto exit;
	}

	ret = ls_do_qp_release(nvme, qp);
	kfree(qp);
	nvme->num_user_qpairs--;

exit:
	return ret;
}

int ls_set_priority(void __user *__args)
{
	int ret = 0;
	uint32_t arb = 0;
	ls_nvme_dev *nvme;
	ls_set_prio_args args = { 0 };

	ret = copy_from_user(&args, __args, sizeof(ls_set_prio_args));
	if (ret < 0) {
		LOG_ERROR("Failed to copy set prio args from user");
		ret = -EFAULT;
		goto exit;
	}

	nvme = nid_to_nvme(args.nid);
	if (args.h_weight == 0 || args.h_weight > 255 || args.m_weight == 0 ||
	    args.m_weight > 255 || args.l_weight == 0 || args.l_weight > 255) {
		LOG_ERROR("Invalid weight");
		ret = -EINVAL;
		goto exit;
	}

	arb |= (args.h_weight << NVME_FEAT_ARB_HPW_SHIFT);
	arb |= (args.m_weight << NVME_FEAT_ARB_MPW_SHIFT);
	arb |= (args.l_weight << NVME_FEAT_ARB_LPW_SHIFT);

	ret = nvme_set_features(&nvme->dev->ctrl, NVME_FEAT_ARBITRATION, arb,
				NULL, 0, NULL);
exit:
	return ret;
}