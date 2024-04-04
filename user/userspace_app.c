#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <linux/mman.h>

#include "dma_test_common.h"

#define DEV_PATH "/dev/svm"

#define PAGE_SIZE 4096
#define MMAP_SIZE PAGE_SIZE * 5

#define COUNTER_MAX 251

/* Adapted from linux/lib/crc8.c (kernel 6.8.2) */
#define CRC8_INIT_VALUE 0xFF
#define CRC8_TABLE_SIZE 256
#define CRC_POLY 0xAB
#define DECLARE_CRC8_TABLE(_table) static uint8_t _table[CRC8_TABLE_SIZE]

DECLARE_CRC8_TABLE(crc_table);

static void crc8_populate_lsb(uint8_t table[CRC8_TABLE_SIZE], uint8_t polynomial)
{
	int i, j;
	uint8_t t = 1;

	table[0] = 0;

	for (i = (CRC8_TABLE_SIZE >> 1); i; i >>= 1) {
		t = (t >> 1) ^ (t & 1 ? polynomial : 0);
		for (j = 0; j < CRC8_TABLE_SIZE; j += 2 * i)
			table[i + j] = table[j] ^ t;
	}
}

static uint8_t crc8(const uint8_t *pdata, size_t nbytes, uint8_t crc)
{
	while (nbytes-- > 0)
		crc = crc_table[(crc ^ *pdata++) & 0xff];

	return crc;
}

enum HugePageType { HP_2M = MAP_HUGE_2MB, HP_1G = MAP_HUGE_1GB };

/**
 * The device writes the following pattern in memory :
 * 0 1 2 3 ... COUNTER_MAX 0 1 2 3 ...
*/
static void assert_writing_succeeded(uint8_t *dst, size_t size)
{
	uint8_t expected = 0;
	for (size_t i = 0; i < size; ++i) {
		if (expected == COUNTER_MAX) {
			expected = 0;
		}
		assert(dst[i] == expected);
		expected += 1;
	}
}

static int do_write(int fd, uint8_t *dst, size_t size)
{
	ioctl(fd, IOCTL_SET_ADDR, &dst);
	ioctl(fd, IOCTL_SET_SIZE, &size);
	return ioctl(fd, IOCTL_START_WRITE);
}

static void* alloc_kernel_mem(int fd, void *src, size_t size)
{
	struct kmem_alloc_request req = {
		.src = src,
		.size = size,
	};
	assert(!ioctl(fd, IOCTL_ALLOC_KMEM, &req));
	return req.res;
}

static void set_opt(int fd, uint64_t opt)
{
	assert(!ioctl(fd, IOCTL_SET_OPT, &opt));
}

/*
 * Ask the device to write deterministic data in a buffer and
 * check the result once the operation is done.
*/
static void write_and_assert(int fd, uint8_t *dst, size_t size)
{
	assert(do_write(fd, dst, size) == 0);
	assert_writing_succeeded(dst, size);
	printf("Write %ld bytes to %p: OK\n", size, dst);
}

static uint8_t fill_mem_with_pseudo_rand(uint8_t *buf, size_t size)
{
	for (size_t i = 0; i < size; ++i) {
		buf[i] = rand();
	}
	return crc8(buf, size, CRC8_INIT_VALUE);
}

static int do_read(int fd, uint8_t *buf, size_t size, uint64_t crc)
{
	ioctl(fd, IOCTL_SET_ADDR, &buf);
	ioctl(fd, IOCTL_SET_SIZE, &size);
	ioctl(fd, IOCTL_SET_CRC, &crc);
	return ioctl(fd, IOCTL_START_READ);
}

/**
 * Ask the device to read a buffer and check that the CRC value is
 * what we are expecting
*/
static void read_and_assert(int fd, uint8_t *buf, size_t size, uint64_t crc)
{
	assert(!do_read(fd, buf, size, crc));
	printf("Read %ld bytes from %p: OK\n", size, buf);
}

/* Helper function to allocate memory that will cause page faults */
static inline void *mmap_internal(void *addr, size_t size)
{
	return mmap(addr, size, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

/**
 * Same as test_svm_read but with write operations
*/
static void test_svm_write(int fd)
{
	printf("%s\n", __func__);
	static uint64_t target_addr = 0xc0ffee000;
	uint8_t *dst, *keep;

	assert(do_write(fd, (void *)target_addr, 1) != 0);

	keep = mmap_internal(NULL, PAGE_SIZE);
	write_and_assert(fd, keep, PAGE_SIZE);

	static const uint8_t reps = 5;
	for (size_t i = 0; i < reps; ++i) {
		dst = mmap_internal((void *)target_addr, MMAP_SIZE);
		write_and_assert(fd, dst, MMAP_SIZE);
		munmap(dst, MMAP_SIZE);
	}

	write_and_assert(fd, keep, PAGE_SIZE);
	munmap(keep, PAGE_SIZE);
}

static void test_svm_write_2(int fd)
{
	printf("%s\n", __func__);
	static uint64_t target_addr = 0xc0ffee000;
	uint8_t *dst, *keep;

	dst = mmap_internal((void *)target_addr, PAGE_SIZE * 5);
	write_and_assert(fd, dst + (PAGE_SIZE * 2), PAGE_SIZE);
	write_and_assert(fd, dst + (PAGE_SIZE * 1), PAGE_SIZE * 2);
	write_and_assert(fd, dst + (PAGE_SIZE * 1), PAGE_SIZE * 3);
	write_and_assert(fd, dst, PAGE_SIZE * 4);
	write_and_assert(fd, dst, PAGE_SIZE * 5);
	munmap(dst, PAGE_SIZE * 5);
}

/**
 * Allocate/deallocate multiple pages and trigger read operations on them.
 * The device will have to ask the kernel to install the missing pages
 * in physical memory.
*/
static void test_svm_read(int fd, uint32_t rand_seed)
{
	printf("%s\n", __func__);
	static uint64_t target_addr = 0xc0ffee000;
	uint8_t *dst, *keep;
	uint64_t keep_crc, crc;

	srand(rand_seed);

	assert(do_read(fd, (void *)target_addr, 1, 0) != 0);

	keep = mmap_internal(NULL, PAGE_SIZE);
	keep_crc = fill_mem_with_pseudo_rand(keep, PAGE_SIZE);
	read_and_assert(fd, keep, PAGE_SIZE, keep_crc);

	static const uint8_t reps = 5;
	for (size_t i = 0; i < reps; ++i) {
		dst = mmap_internal((void *)target_addr, MMAP_SIZE);
		crc = fill_mem_with_pseudo_rand(dst, MMAP_SIZE);
		read_and_assert(fd, dst, MMAP_SIZE, crc);
		munmap(dst, MMAP_SIZE);
	}

	/* The first CRC is still valid */
	read_and_assert(fd, keep, PAGE_SIZE, keep_crc);
	munmap(keep, PAGE_SIZE);
}

/**
 * Try to write overlapping virtual (but not physical) regions
 * in 2 address spaces
*/
static void test_fork(uint32_t rand_seed)
{
	printf("%s\n", __func__);
	static uint64_t target_addr = 0xc0ffee000;
	int fd;
	uint8_t *dst, *dst_child;
	uint64_t crc;
	int pid;
	size_t size;

	srand(rand_seed);
	fd = open(DEV_PATH, O_RDWR);
	dst = mmap_internal((void *)target_addr, MMAP_SIZE);
	write_and_assert(fd, dst, MMAP_SIZE);
	close(fd);

	pid = fork();
	if (pid) {
		/* parent */
		waitpid(pid, NULL, 0);
		/* Data in the buffer should be the same as before */
		assert_writing_succeeded(dst, MMAP_SIZE);
		munmap(dst, MMAP_SIZE);
	} else {
		/* child*/
		fd = open(DEV_PATH, O_RDWR);
		/**
         * Don't write the exact same pattern as the parent so that we can
         * check there is no collision
        */
		size = MMAP_SIZE - PAGE_SIZE;
		dst_child = dst + PAGE_SIZE;
		write_and_assert(fd, dst_child, size);
		munmap(dst, size);
		close(fd);
	}
}

static void do_test_huge_page(int fd, size_t alloc_size, size_t op_size,
			      enum HugePageType hp_size)
{
	uint8_t *dst;
	dst = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | hp_size, -1, 0);
	if (dst == MAP_FAILED) {
		perror("test_huge_page");
		assert(false);
	}

	write_and_assert(fd, dst, op_size);
	munmap(dst, alloc_size);
}

/**
 * Test huge pages (2Mb and 1Gb)
 * Start operations on large physically contiguous memory regions
*/
static void test_huge_pages(int fd)
{
	printf("%s\n", __func__);
	size_t hp_2m = PAGE_SIZE * (PAGE_SIZE / 8);
	size_t hp_1g = hp_2m * (PAGE_SIZE / 8);
	do_test_huge_page(fd, hp_2m, PAGE_SIZE, HP_2M);
	do_test_huge_page(fd, hp_2m, PAGE_SIZE + 1, HP_2M);
	do_test_huge_page(fd, hp_2m, hp_2m, HP_2M);
	do_test_huge_page(fd, hp_2m * 10, (hp_2m * 7) + PAGE_SIZE, HP_2M);
	do_test_huge_page(fd, hp_1g, PAGE_SIZE, HP_1G);
	do_test_huge_page(fd, hp_1g, hp_1g, HP_1G);
}

/**
 * Test the permissions
 * We should be able to read but not to write to the read only mapping
*/
static void test_permission_error(int fd)
{
	printf("%s\n", __func__);
	uint8_t *dst, *dst_ro;
	int memfd;
	uint8_t crc;

	/* create rw and ro mappings */
	memfd = memfd_create("revoke_write", 0);
	assert(!ftruncate(memfd, PAGE_SIZE));
	dst = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
	dst_ro = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, memfd, 0);

	/* prepare the memory region */
	crc = fill_mem_with_pseudo_rand(dst, PAGE_SIZE);

	/* we can read from both dst_ro and dst */
	read_and_assert(fd, dst_ro, PAGE_SIZE, crc);
	read_and_assert(fd, dst, PAGE_SIZE, crc);

	/* we can write to dst but not to dst_ro */
	assert(do_write(fd, dst_ro, PAGE_SIZE) != 0);
	write_and_assert(fd, dst, PAGE_SIZE);

	/* clean the region */
	memset(dst, 0x0, PAGE_SIZE);
	assert(dst[0] == 0 && dst[1] == 0);
	assert(dst_ro[0] == 0 && dst_ro[1] == 0);

	/* write to dst and read from dst_ro */
	assert(do_write(fd, dst, PAGE_SIZE) == 0);
	assert_writing_succeeded(dst_ro, PAGE_SIZE);

	munmap(dst_ro, PAGE_SIZE);
	munmap(dst, PAGE_SIZE);
	close(memfd);
}

static void test_stack_operations_reset_data(uint8_t *byte, uint16_t *word,
											 uint32_t *dword, uint64_t *qword)
{
	*byte = 0;
	*word = 0;
	*dword = 0;
	*qword = 0;
}

static void test_stack_operations(int fd)
{
	printf("%s\n", __func__);
	static const size_t buffer_size = 256;
	uint8_t crc;
	uint8_t buffer[buffer_size];
	uint8_t byte = 0;
	uint16_t word = 0;
	uint32_t dword = 0;
	uint64_t qword = 0;

	write_and_assert(fd, &byte, sizeof(byte));
	assert(!word);
	assert(!dword);
	assert(!qword);
	test_stack_operations_reset_data(&byte, &word, &dword, &qword);

	write_and_assert(fd, (uint8_t*)&word, sizeof(word));
	assert(!byte);
	assert(!dword);
	assert(!qword);
	test_stack_operations_reset_data(&byte, &word, &dword, &qword);

	write_and_assert(fd, (uint8_t*)&dword, sizeof(dword));
	assert(!byte);
	assert(!word);
	assert(!qword);
	test_stack_operations_reset_data(&byte, &word, &dword, &qword);

	write_and_assert(fd, (uint8_t*)&qword, sizeof(qword));
	assert(!byte);
	assert(!word);
	assert(!dword);
	test_stack_operations_reset_data(&byte, &word, &dword, &qword);

	qword = 0xcafecafedeadc0deULL;
	crc = crc8((uint8_t*)&qword, sizeof(qword), CRC8_INIT_VALUE);
	read_and_assert(fd, (uint8_t*)&qword, sizeof(qword), crc);

	test_stack_operations_reset_data(&byte, &word, &dword, &qword);
	write_and_assert(fd, buffer, buffer_size);
	assert(!byte);
	assert(!word);
	assert(!dword);
	assert(!qword);
}

static void test_interrupt_range(int fd)
{
	printf("%s\n", __func__);
	for (void *it_range = (void*)0xfee00000ULL; it_range < (void*)0xfeefffffULL;
		 it_range += 0x1000) {
		assert(do_write(fd, it_range, 1) != 0);
	}
}

/**
 * Check that the device cannot translate addresses in privileged mode
*/
static void test_kernel_mem_access(int fd)
{
	printf("%s\n", __func__);
	static size_t buf_size = 4096;

	uint8_t src_buf[buf_size];
	void *kbuf;
	uint8_t crc;

	crc = fill_mem_with_pseudo_rand(src_buf, buf_size);
	kbuf = alloc_kernel_mem(fd, src_buf, buf_size);

	/*
	 * access in privileged mode not supported
	 * (should fail in both kernel and user memory)
	 */
	set_opt(fd, OPT_PRIV);
	assert(do_read(fd, kbuf, buf_size, crc) != 0);
	assert(do_read(fd, src_buf, buf_size, crc) != 0);
	set_opt(fd, OPT_NONE);

	/*
	 * kernel memory access in user mode should always fail
	 * but user memory can be read
	 */
	assert(do_read(fd, kbuf, buf_size, crc) != 0);
	assert(!do_read(fd, src_buf, buf_size, crc));

}

static void test_pri_stress(int fd) {
	static size_t size = PAGE_SIZE * 10;
	uint8_t *buf = mmap_internal(NULL, size);
	size_t op_size;

	assert(madvise(buf, size, MADV_PAGEOUT | MADV_RANDOM) == 0);
	for (unsigned i = 0; i < 500; ++i) {
		op_size = (i * PAGE_SIZE) % (size + PAGE_SIZE);
		if (op_size == 0) {
			continue;
		}
		write_and_assert(fd, buf, op_size);
		assert(madvise(buf, size, MADV_PAGEOUT | MADV_RANDOM) == 0);
		usleep(10000);
	}
}

int main(int argc, char *argv[])
{
	crc8_populate_lsb(crc_table, CRC_POLY);

	int fd = open(DEV_PATH, O_RDWR);
	set_opt(fd, OPT_NONE);
	test_svm_write(fd);
	test_svm_read(fd, 0xb001ea00);
	test_svm_read(fd, 0xb001ea0f);
	test_svm_write_2(fd);
	test_huge_pages(fd);
	test_permission_error(fd);
	test_stack_operations(fd);
	test_interrupt_range(fd);
	test_kernel_mem_access(fd);
	test_pri_stress(fd);
	close(fd);

	test_fork(0xbee);
	return 0;
}
