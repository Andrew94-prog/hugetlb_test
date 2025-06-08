#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mount.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>

static void safe_file_printf(const char *path, const char *fmt, ...)
{
	va_list va;
	FILE *f;

	va_start(va, fmt);

	f = fopen(path, "w");
	if (f == NULL) {
		printf("Failed to open FILE '%s' for writing", path);
		return;
	}

	if (vfprintf(f, fmt, va) < 0) {
		printf("Failed to print to FILE '%s'", path);
		return;
	}

	if (fclose(f)) {
		printf("Failed to close FILE '%s'", path);
		return;
	}

	va_end(va);
}


static int tst_count_scanf_conversions(const char *fmt)
{
	unsigned int cnt = 0;
	int flag = 0;

	while (*fmt) {
		switch (*fmt) {
		case '%':
			if (flag) {
				cnt--;
				flag = 0;
			} else {
				flag = 1;
				cnt++;
			}
			break;
		case '*':
			if (flag) {
				cnt--;
				flag = 0;
			}
			break;
		default:
			flag = 0;
		}

		fmt++;
	}

	return cnt;
}

/*
 * Try to parse each line from file specified by 'path' according
 * to scanf format 'fmt'. If all fields could be parsed, stop and
 * return 0, otherwise continue or return 1 if EOF is reached.
 */
static int file_lines_scanf(const char *path, const char *fmt, ...)
{
	FILE *fp;
	int ret = 0;
	int arg_count = 0;
	char line[BUFSIZ];
	va_list ap;

	if (!fmt) {
		printf("pattern is NULL");
		return 1;
	}

	fp = fopen(path, "r");
	if (fp == NULL) {
		printf("Failed to open FILE '%s' for reading", path);
		return 1;
	}

	arg_count = tst_count_scanf_conversions(fmt);

	while (fgets(line, BUFSIZ, fp) != NULL) {
		va_start(ap, fmt);
		ret = vsscanf(line, fmt, ap);
		va_end(ap);

		if (ret == arg_count)
			break;
	}
	fclose(fp);
	if (ret != arg_count) {
		printf("Expected %i conversions got %i FILE '%s'",
				arg_count, ret, path);
		return 1;
	}

	return !(ret == arg_count);
}


#define SAFE_FILE_LINES_SCANF(path, fmt, ...) \
	file_lines_scanf((path), (fmt), ## __VA_ARGS__)

#define SAFE_READ_MEMINFO(item) \
	({long tst_rval; \
	 SAFE_FILE_LINES_SCANF("/proc/meminfo", item " %ld", \
			 &tst_rval); \
			 tst_rval;})

static void read_meminfo_huge(long *total, long *free, long *resv, long *surp)
{
	*total = SAFE_READ_MEMINFO("HugePages_Total:");
	*free = SAFE_READ_MEMINFO("HugePages_Free:");
	*resv = SAFE_READ_MEMINFO("HugePages_Rsvd:");
	*surp = SAFE_READ_MEMINFO("HugePages_Surp:");
}

static int verify_hpage_counters(char *desc, long et,
			long ef, long er, long es)
{
	long t, f, r, s;
	long fail = 0;

	read_meminfo_huge(&t, &f, &r, &s);

	if (t != et) {
		printf("%s: Bad total expected %li, actual %li\n", desc, et, t);
		fail++;
	}               
	if (f != ef) {
		printf("%s: Bad free expected %li, actual %li\n", desc, ef, f);
		fail++;
	}
	if (r != er) {
		printf("%s: Bad rsvd expected %li, actual %li\n", desc, er, r);
		fail++;
	}
	if (s != es) {
		printf("%s: Bad surp expected %li, actual %li\n", desc, es, s);
		fail++;
	}

	if (fail)
		return -1;

	return 0;
}


/*
 * argv[1] - initial number of huge pages in system to allocate
 * argv[2] - maximum number of huge pages, which is allowed to be
 * allocated (uncluding overcommit)
 * Test mmaps argv[2] number of huge pages, touches each page and
 * unmaps all pages page-by-page. After each step test checks huge
 * page counters in system.
 *
 * Example:
 * ./huge_page_test 100 120
 */
int main(int argc, char *argv[])
{
	void *addr = NULL, *buf = NULL;
	int i, n_init = atoi(argv[1]), n_mmap = atoi(argv[2]);
	long huge_page_size = SAFE_READ_MEMINFO("Hugepagesize:") * 1024;

	/* Set initial number of huge pages in system */
	safe_file_printf("/proc/sys/vm/nr_hugepages", "%lu", n_init);
	safe_file_printf("/proc/sys/vm/nr_overcommit_hugepages", "%lu",
			n_mmap - n_init);
	printf("Start test: nr_hugepages = %d, nr_overcommit_hugepages = %d, "
		" mmap %d huge pages, huge_page_size = %ldK\n", n_init,
		n_mmap - n_init, n_mmap, huge_page_size / 1024);

	/* Mmap buffer by huge pages */
	buf = mmap(NULL, huge_page_size * n_mmap, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (!buf) {
		printf("Mmap of buffer by huge pages failed\n");
		return 1;
	}
	verify_hpage_counters("After mmap", n_mmap, n_mmap, n_mmap, n_mmap - n_init);
	printf("Buffer is mmaped by huge pages to addr = %p\n", buf);

	/* Touch each huge page in buffer */
	for (i = 0; i < n_mmap; i++) {
		*((char *) buf + i * huge_page_size) = 1;
	}
	verify_hpage_counters("After touch", n_mmap, 0, 0, n_mmap - n_init);
	printf("Each page in buffer is touched\n");

	/* Unmap buffer page-by-page */
	for (i = 0; i < n_mmap; i++) {
		munmap(buf + (n_mmap - 1 - i) * huge_page_size, huge_page_size);
		verify_hpage_counters("After munmap",
				(n_mmap - i - 1) > n_init ? n_mmap - i - 1 : n_init,
				(n_mmap - i - 1) > n_init ? 0 : i + 1 - (n_mmap - n_init), 0,
				(n_mmap - i - 1) > n_init ? n_mmap - n_init - i - 1 : 0);
	}
	printf("Huge page in buffer is unmapped\n");

	return 0;
}
