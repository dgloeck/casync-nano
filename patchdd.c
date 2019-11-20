#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>

#include "utils.h"

#define TRANSFER_SIZE (1024 * 1024)

static size_t memcmp_offset(uint8_t *a, uint8_t *b, size_t len)
{
	size_t offset;

	for (offset = 0; offset < len; offset++)
		if (a[offset] != b[offset])
			break;

	return offset;
}

static void progress(int percent)
{
	static size_t calls = 0;
	static const char spinner[] = "-\\|//";

	printf("\033[0G[%c] %3d%%", spinner[calls++ % strlen(spinner)], percent);

	if (percent == 100)
		putchar('\n');

	fflush(stdout);
}

int main(int argc, char **argv)
{
	u_log_init();

	if (argc < 3 ||
	    (argc >= 2 && (streq(argv[1], "-h") || streq(argv[1], "--help")))) {
		fprintf(stderr, "usage: %s src dst\n", argv[0]);
		return EXIT_FAILURE;
	}

	int src, dst;
	src = open(argv[1], O_RDONLY);
	if (src < 0) {
		u_log_errno("opening source '%s' failed", argv[1]);
		return EXIT_FAILURE;
	}

	dst = open(argv[2], O_RDWR);
	if (dst < 0) {
		u_log_errno("opening destination '%s' failed", argv[2]);
		return EXIT_FAILURE;
	}

	off_t src_size, dst_size;
	checked(fd_size(src, &src_size), return EXIT_FAILURE);
	checked(fd_size(dst, &dst_size), return EXIT_FAILURE);

	if (dst_size < src_size) {
		u_log(ERR, "target size (%llu) is smaller than source size (%llu)",
		      (unsigned long long)dst_size, (unsigned long long)src_size);
		return EXIT_FAILURE;
	}

	if (src_size < dst_size)
		u_log(INFO, "target size (%llu) is larger than source size (%llu)",
		      (unsigned long long)dst_size, (unsigned long long)src_size);

	time_t now = time_monotonic();
	time_t start = now;

	uint8_t src_buf[TRANSFER_SIZE];
	uint8_t dst_buf[TRANSFER_SIZE];

	bool print_progress = isatty(STDOUT_FILENO);

	off_t offset = 0;
	off_t total_skipped = 0;
	while (offset < src_size) {
		size_t to_read = MIN(src_size - offset, TRANSFER_SIZE);

		if (preadall(src, src_buf, to_read, offset) < 0)
			return EXIT_FAILURE;

		if (preadall(dst, dst_buf, to_read, offset) < 0)
			return EXIT_FAILURE;

		size_t first_diff = memcmp_offset(src_buf, dst_buf, to_read);
		total_skipped += first_diff;

		if (pwriteall(dst, &src_buf[first_diff], to_read - first_diff, offset + first_diff) < 0)
			return EXIT_FAILURE;

		offset += to_read;

		if (print_progress)
			progress((100ull * offset) / src_size);
	}

	now = time_monotonic();
	printf("synchronization finished after %u seconds\n", (unsigned int)(now - start));
	printf("skipped a total of %llu bytes (%.2f%% of the input)\n",
	      (unsigned long long)total_skipped,
	      (double)((100.0 * total_skipped)/src_size));

	return EXIT_SUCCESS;
}
