#include "libstorage_api.h"
#include "libstorage_common.h"
#include "unistd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BATCH_SIZE     3
#define HUGE_PAGE_SIZE (2 * 1024 * 1024)

ls_nvme_dev nvme;
ls_nvme_qp qp;
dma_buffer dma;
int test_cnt;

const int batch = BATCH_SIZE;
dma_buffer wrbuf[BATCH_SIZE];
dma_buffer rdbuf[BATCH_SIZE];
int cb_cnt;
static void async_callback(void *data)
{
	cb_cnt++;
	fprintf(stderr, "Cb : %d\n", cb_cnt);
}
static void test_sync_polling(int instance_id)
{
	int ret;
	uint64_t lba = HUGE_PAGE_SIZE;
	uint32_t count = 1024 * 4;
	ls_queue_args qp_args;

	qp_args.nid = nvme.id;
	qp_args.interrupt = LS_INTERRPUT_POLLING;
	qp_args.depth = 128;
	qp_args.num_requests = 256;
	qp_args.instance_id = instance_id;
	ret = create_qp(&qp_args, &nvme, &qp);
	if (ret) {
		printf("Failed to create qp\n");
		return;
	}
	ret = create_dma_buffer(&nvme, &dma, HUGE_PAGE_SIZE);
	if (ret) {
		printf("Failed to create dma buffer\n");
		return;
	}

	sprintf(dma.buf, "Fuck u World!!\n");
	ret = write_blk(&qp, lba, count, &dma);
	if (ret) {
		printf("Failed to write block\n");
		return;
	}

	memset(dma.buf, 0, HUGE_PAGE_SIZE);
	ret = read_blk(&qp, lba, count, &dma);
	printf("%d. Successfully read/write block, str : %20s\n", ++test_cnt,
	       (char *)dma.buf);
	getchar();

	delete_dma_buffer(&nvme, &dma);
	delete_qp(&qp_args, &nvme, &qp);
}
static void test_read_write_correctness(int upid_idx)
{
	ls_queue_args qp_args;
	int ret;
	int iosize = 1024 * 4;
	memset(&qp_args, 0, sizeof qp_args);
	qp_args.nid = nvme.id;
	qp_args.interrupt = LS_INTERRPUT_UINTR;
	qp_args.depth = 128;
	qp_args.num_requests = 128;
	qp_args.upid_idx = upid_idx;
	ret = create_qp(&qp_args, &nvme, &qp);
	if (ret) {
		printf("Failed to Create QP");
		return;
	}

	for (int i = 0; i < batch; i++) {
		create_dma_buffer(&nvme, wrbuf + i, iosize);
		create_dma_buffer(&nvme, rdbuf + i, iosize);
		memset(wrbuf[i].buf, 0x2f, iosize);
		memset(rdbuf[i].buf, 0, iosize);
	}

	for (int i = 0; i < batch; i++) {
		ret = write_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				      wrbuf + i, async_callback, NULL);
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	printf("write success\n");
	for (int i = 0; i < batch; i++) {
		ret = read_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				     rdbuf + i, async_callback, NULL);
		if (ret) {
			printf("failed to send req!\n");
		}
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	int byte_cnt = 0;
	for (int i = 0; i < batch; i++) {
		char *buf = (char *)rdbuf[i].buf;
		for (int j = 0; j < iosize; j++) {
			byte_cnt += (buf[j] == 0x2f);
			assert(buf[j] == 0x2f);
		}
	}
	printf("%d. Successfully read/write block, byte_cnt : %d, expected : %d\n",
	       ++test_cnt, byte_cnt, batch * iosize);

	for (int i = 0; i < batch; i++) {
		delete_dma_buffer(&nvme, wrbuf + i);
		delete_dma_buffer(&nvme, rdbuf + i);
	}
	delete_qp(&qp_args, &nvme, &qp);
	getchar();
}

static void test_read_write(int upid_idx)
{
	ls_queue_args qp_args;
	int ret;
	int iosize = 1024 * 4;
	memset(&qp_args, 0, sizeof qp_args);
	qp_args.nid = nvme.id;
	qp_args.interrupt = LS_INTERRPUT_UINTR;
	qp_args.depth = 128;
	qp_args.num_requests = 128;
	qp_args.instance_id = upid_idx;
	ret = create_qp(&qp_args, &nvme, &qp);
	if (ret) {
		printf("Failed to Create QP");
		return;
	}

	for (int i = 0; i < batch; i++) {
		create_dma_buffer(&nvme, wrbuf + i, iosize);
		create_dma_buffer(&nvme, rdbuf + i, iosize);
		memset(wrbuf[i].buf, 0x2f, iosize);
		memset(rdbuf[i].buf, 0, iosize);
	}

	for (int i = 0; i < batch; i++) {
		ret = write_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				      wrbuf + i, async_callback, NULL);
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	printf("write success\n");
	for (int i = 0; i < batch; i++) {
		ret = read_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				     rdbuf + i, async_callback, NULL);
		if (ret) {
			printf("failed to send req!\n");
		}
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	int byte_cnt = 0;
	for (int i = 0; i < batch; i++) {
		char *buf = (char *)rdbuf[i].buf;
		for (int j = 0; j < iosize; j++) {
			byte_cnt += (buf[j] == 0x2f);
			assert(buf[j] == 0x2f);
		}
	}
	printf("%d. Successfully read/write block, byte_cnt : %d, expected : %d\n",
	       ++test_cnt, byte_cnt, batch * iosize);

	for (int i = 0; i < batch; i++) {
		delete_dma_buffer(&nvme, wrbuf + i);
		delete_dma_buffer(&nvme, rdbuf + i);
	}

	delete_qp(&qp_args, &nvme, &qp);
	getchar();
}
static void test_huge_single_request(int upid_idx)
{
	ls_queue_args qp_args;
	int ret;
	int iosize = 1024 * 128;
	memset(&qp_args, 0, sizeof qp_args);
	qp_args.nid = nvme.id;
	qp_args.interrupt = LS_INTERRPUT_UINTR;
	qp_args.depth = 128;
	qp_args.num_requests = 128;
	qp_args.upid_idx = upid_idx;
	ret = create_qp(&qp_args, &nvme, &qp);
	if (ret) {
		printf("Failed to Create QP");
		return;
	}

	for (int i = 0; i < batch; i++) {
		create_dma_buffer(&nvme, wrbuf + i, iosize);
		create_dma_buffer(&nvme, rdbuf + i, iosize);
		memset(wrbuf[i].buf, 0x1b, iosize);
		memset(rdbuf[i].buf, 0, iosize);
	}

	for (int i = 0; i < batch; i++) {
		ret = write_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				      wrbuf + i, async_callback, NULL);
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	printf("write success\n");
	for (int i = 0; i < batch; i++) {
		ret = read_blk_async(&qp, HUGE_PAGE_SIZE + iosize * i, iosize,
				     rdbuf + i, async_callback, NULL);
		if (ret) {
			printf("failed to send req!\n");
		}
	}
	while (cb_cnt < batch) {
	}
	cb_cnt = 0;
	int byte_cnt = 0;
	for (int i = 0; i < batch; i++) {
		char *buf = (char *)rdbuf[i].buf;
		for (int j = 0; j < iosize; j++) {
			byte_cnt += ((buf[j]) == 0x1b);
			if ((buf[j]) != 0x1b) {
				printf("buf[%d] : %x\n", j, buf[j]);
			}
		}
	}
	printf("%d. Successfully read/write block, byte_cnt : %d, expected : %d\n",
	       ++test_cnt, byte_cnt, batch * iosize);
	getchar();
	for (int i = 0; i < batch; i++) {
		delete_dma_buffer(&nvme, wrbuf + i);
		delete_dma_buffer(&nvme, rdbuf + i);
	}
	delete_qp(&qp_args, &nvme, &qp);
}
static void test_request_split(int upid_idx)
{
	int ret;
	uint64_t lba = HUGE_PAGE_SIZE;
	uint32_t count = 256 * 1024;
	ls_queue_args qp_args;

	qp_args.nid = nvme.id;
	qp_args.interrupt = LS_INTERRPUT_UINTR;
	qp_args.depth = 128;
	qp_args.num_requests = 256;
	qp_args.upid_idx = upid_idx;
	ret = create_qp(&qp_args, &nvme, &qp);
	if (ret) {
		printf("Failed to create qp\n");
		return;
	}

	ret = create_dma_buffer(&nvme, &dma, HUGE_PAGE_SIZE);
	if (ret) {
		printf("Failed to create dma buffer\n");
		return;
	}
	memset(dma.buf, 0x2d, count);
	ret = write_blk(&qp, lba, count, &dma);
	if (ret) {
		printf("Failed to write block\n");
		return;
	}

	memset(dma.buf, 0, count);
	ret = read_blk(&qp, lba, count, &dma);
	int byte_cnt = 0;
	unsigned char *buf = (unsigned char *)dma.buf;
	sleep(1);
	for (int i = 0; i < count; i++) {
		byte_cnt += (buf[i] == 0x2d);
		if (buf[i] != 0x2d) {
			printf("buf[%x] : %x\n", i, buf[i]);
			break;
		}
	}
	printf("%d. Successfully read/write block, byte_cnt : %d, expected : %d\n",
	       ++test_cnt, byte_cnt, count);

	delete_dma_buffer(&nvme, &dma);
	delete_qp(&qp_args, &nvme, &qp);
}
int main()
{
	ls_device_args dev_args;

	int ret;

	strcpy(dev_args.path, "/dev/nvme0");
	ret = open_device(&dev_args, &nvme);
	if (ret) {
		printf("Failed to open device\n");
		return -1;
	}
	ret = register_uintr(&dev_args, &nvme);
	// test_sync_polling(dev_args.instance_id);
	// test_read_write_correctness(dev_args.upid_idx);
	getchar();
	test_read_write(dev_args.instance_id);
	// test_huge_single_request(dev_args.upid_idx);
	// test_request_split(dev_args.upid_idx);
	close_device(&dev_args, &nvme);
	return 0;
}