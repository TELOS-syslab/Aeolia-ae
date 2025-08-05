#ifndef _DMA_POOL_H
#define _DMA_POOL_H

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <unistd.h>

#define HUGE_DIR       "/mnt/libstorage_huge"
#define HUGE_PAGE_SIZE (2ul * 1024 * 1024)

typedef struct dma_pool {
	uint64_t size;
	uint64_t bitmap_len;
	uint64_t free_size;
	int pool_fd;
	void *buf;
	uint64_t *pa_base;
	uint64_t *free_bitmap;

	uint32_t last_alloc_map;
	uint32_t last_alloc_bit;

	char *pool_path;
} dma_pool;

int create_dma_pool(struct dma_pool *pool, uint64_t size, int instance_id);
int delete_dma_pool(struct dma_pool *pool);

int alloc_dma_buffer(struct dma_pool *pool, uint32_t size, uint64_t *buf,
		     uint64_t **pa);
int free_dma_buffer(struct dma_pool *pool, void *buf, uint32_t size);

#endif