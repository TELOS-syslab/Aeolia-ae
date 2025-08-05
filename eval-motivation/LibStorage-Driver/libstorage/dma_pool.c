#include "dma_pool.h"
#include "logger.h"

// This function will cause some syscall, so do not call it in the critical
// path.
static int virt_to_phys(void *va, uint64_t *pa)
{
	int fd;
	unsigned long va_page = (unsigned long)va & ~(PAGE_SIZE - 1);
	unsigned long offset = (unsigned long)va & (PAGE_SIZE - 1);
	char pagemap_file[64];
	uint64_t entry;

	sprintf(pagemap_file, "/proc/%d/pagemap", getpid());
	fd = open(pagemap_file, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	if (pread(fd, &entry, sizeof(entry),
		  (va_page / PAGE_SIZE) * sizeof(entry)) != sizeof(entry)) {
		close(fd);
		return -1;
	}

	close(fd);

	if (!(entry & (1ULL << 63))) {
		return -1;
	}

	*pa = (entry & ((1ULL << 55) - 1)) * PAGE_SIZE + offset;
	return 0;
}

static unsigned long get_free_hugepages(void)
{
	unsigned long fhp = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return fhp;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "HugePages_Free:      %lu", &fhp) == 1)
			break;
	}

	free(line);
	fclose(f);
	return fhp;
}

int create_dma_pool(struct dma_pool *pool, uint32_t size, int instance_id)
{
	char *path;
	unsigned long free_hpages;
	int result;

	free_hpages = get_free_hugepages();
	if (size % HUGE_PAGE_SIZE != 0 || size > free_hpages * HUGE_PAGE_SIZE) {
		LOG_ERROR("Invalid size : %d, free_hpage : %lu", size,
			  free_hpages);
		return -1;
	}
	pool->pool_path = malloc(256);
	path = pool->pool_path;

	sprintf(path, "%s/%d", HUGE_DIR, instance_id);
	pool->pool_fd = open(path, O_RDWR | O_CREAT | O_SYNC, 0755);
	if (pool->pool_fd < 0) {
		LOG_ERROR("open failed");
		close(pool->pool_fd);
		return -1;
	}
	if (ftruncate(pool->pool_fd, size) != 0) {
		LOG_ERROR("ftruncate failed");
		close(pool->pool_fd);
		unlink(path);
		return -1;
	}

	pool->size = size;
	pool->free_size = size;
	// is MAP_POPULATE successful?
	pool->buf = mmap(NULL, pool->size, PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_POPULATE, pool->pool_fd, 0);

	if (pool->buf == MAP_FAILED) {
		LOG_ERROR("mmap failed\n");
		close(pool->pool_fd);
		unlink(path);
		return -1;
	}

	pool->pa_base = malloc(sizeof(uint64_t) * (size / PAGE_SIZE));
	for (int i = 0; i < size / HUGE_PAGE_SIZE; i++) {
		result = virt_to_phys(pool->buf + i * HUGE_PAGE_SIZE,
				      &pool->pa_base[i * 512]);
		if (result == -1) {
			LOG_ERROR("virt_to_phys failed\n");
			close(pool->pool_fd);
			unlink(path);
			return -1;
		}
		for (int j = 0; j < 512; j++) {
			pool->pa_base[i * 512 + j] =
				pool->pa_base[i * 512] + j * PAGE_SIZE;
		}
	}

	pool->bitmap_len = pool->size / PAGE_SIZE / 64;
	pool->free_bitmap = malloc(pool->bitmap_len * sizeof(uint64_t));
	memset(pool->free_bitmap, 0, pool->bitmap_len * sizeof(uint64_t));
	// pool->free_bitmap = malloc(pool->bitmap_len);
	// memset(pool->free_bitmap, 0, pool->bitmap_len);
	return 0;
}

int delete_dma_pool(struct dma_pool *pool)
{
	munmap(pool->buf, pool->size);
	free(pool->free_bitmap);
	free(pool->pa_base);
	close(pool->pool_fd);
	LOG_DEBUG("unlink %s\n", pool->pool_path);
	if (unlink(pool->pool_path) == 0) {
		LOG_DEBUG("unlink success\n");
	} else {
		LOG_ERROR("unlink failed %s\n", pool->pool_path);
	}
	return 0;
}

int alloc_dma_buffer(struct dma_pool *pool, uint32_t size, uint64_t *va,
		     uint64_t **pa)
{
	uint32_t i, len;
	if (size % PAGE_SIZE != 0) {
		return -1;
	}
	// TODO: resize the hugepage file.
	if (size > pool->size) {
		return -1;
	}
	len = size / PAGE_SIZE;

	uint32_t count = 0;
	uint32_t start = 0;

	for (i = 0; i < pool->bitmap_len; i++) {
		if (pool->free_bitmap[i] == ~(uint64_t)0) {
			count = 0;
			continue;
		}
		for (int j = 0; j < 64; j++) {
			if ((pool->free_bitmap[i] & (1 << j)) == 0) {
				if (count == 0) {
					start = i * 64 + j;
				}
				count++;
				if (count == len) {
					goto found;
				}
			} else {
				count = 0;
			}
		}
	}
	return -1;
found:
	for (i = 0; i < len; i++) {
		pool->free_bitmap[(start + i) / 64] |= 1 << ((start + i) % 64);
	}
	*va = (uint64_t)pool->buf + start * PAGE_SIZE;
	*pa = &pool->pa_base[start];
	if (pool->buf == NULL) {
		exit(0);
	}
	// printf("alloc va : %p, pa : %p\n", va, pa);
	// __asm__("int $0x03");
	return 0;
}

int free_dma_buffer(struct dma_pool *pool, void *buf, uint32_t size)
{
	int i, len;
	if ((uint64_t)buf < (uint64_t)pool->buf ||
	    (uint64_t)buf >= (uint64_t)pool->buf + pool->size) {
		return -1;
	}
	if ((uint64_t)buf % PAGE_SIZE != 0) {
		return -1;
	}
	if (size % PAGE_SIZE != 0) {
		return -1;
	}
	len = size / PAGE_SIZE;
	int index = ((uint64_t)buf - (uint64_t)pool->buf) / PAGE_SIZE;
	for (i = 0; i < len; i++) {
		pool->free_bitmap[(index + i) / 64] &=
			~(1 << ((index + i) % 64));
	}
	return 0;
}
