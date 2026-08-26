// SPDX-License-Identifier: GPL-2.0
/* Validate PAGEOUT followed by synchronous remote MADV_POPULATE_READ. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif
#ifndef __NR_process_madvise
#define __NR_process_madvise 440
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif
#ifndef MADV_POPULATE_READ
#define MADV_POPULATE_READ 22
#endif

#define TEST_MIB 64
#define KSFT_SKIP 4

static long process_madvise_one(int pidfd, void *base, size_t len, int advice)
{
	struct iovec iov = {
		.iov_base = base,
		.iov_len = len,
	};

	return syscall(__NR_process_madvise, pidfd, &iov, 1, advice, 0);
}

static long status_kib(pid_t pid, const char *key)
{
	char path[64], line[256];
	FILE *file;
	long value = -1;

	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	file = fopen(path, "re");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%*s %ld kB", &value) == 1 &&
		    !strncmp(line, key, strlen(key)))
			break;
		value = -1;
	}
	fclose(file);
	return value;
}

static void terminate_child(pid_t child)
{
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
}

static int child_main(int command_fd, int report_fd, size_t len)
{
	const size_t pages = len / (size_t)getpagesize();
	unsigned char *mapping;
	unsigned long long checksum = 0;
	uintptr_t address;
	char command;
	size_t i;

	mapping = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return 1;
	for (i = 0; i < pages; i++)
		mapping[i * (size_t)getpagesize()] = i & 0xff;

	address = (uintptr_t)mapping;
	if (write(report_fd, &address, sizeof(address)) != sizeof(address))
		return 1;
	if (read(command_fd, &command, 1) != 1 || command != 'c')
		return 1;

	for (i = 0; i < pages; i++)
		checksum += mapping[i * (size_t)getpagesize()];
	if (write(report_fd, &checksum, sizeof(checksum)) != sizeof(checksum))
		return 1;
	return 0;
}

int main(void)
{
	const size_t len = TEST_MIB * 1024UL * 1024UL;
	const size_t pages = len / (size_t)getpagesize();
	unsigned long long checksum, expected = 0;
	int command_pipe[2], report_pipe[2];
	long rss_pageout, rss_populate;
	long swap_pageout, swap_populate;
	uintptr_t address;
	pid_t child;
	int pidfd, status;
	long ret;
	size_t i;

	if (pipe2(command_pipe, O_CLOEXEC) || pipe2(report_pipe, O_CLOEXEC)) {
		perror("pipe2");
		return 1;
	}
	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	if (!child) {
		close(command_pipe[1]);
		close(report_pipe[0]);
		_exit(child_main(command_pipe[0], report_pipe[1], len));
	}
	close(command_pipe[0]);
	close(report_pipe[1]);
	if (read(report_pipe[0], &address, sizeof(address)) != sizeof(address)) {
		fprintf(stderr, "child did not publish its mapping\n");
		terminate_child(child);
		return 1;
	}

	pidfd = syscall(__NR_pidfd_open, child, 0);
	if (pidfd < 0) {
		perror("pidfd_open");
		terminate_child(child);
		return 1;
	}
	ret = process_madvise_one(pidfd, (void *)address, len, MADV_PAGEOUT);
	if (ret < 0) {
		int rc = errno == ENOSYS || errno == EINVAL ? KSFT_SKIP : 1;

		perror("process_madvise PAGEOUT");
		terminate_child(child);
		return rc;
	}
	usleep(250000);
	rss_pageout = status_kib(child, "VmRSS:");
	swap_pageout = status_kib(child, "VmSwap:");
	if (swap_pageout <= 0) {
		fprintf(stderr, "SKIP: PAGEOUT produced no observable swap\n");
		terminate_child(child);
		return KSFT_SKIP;
	}

	ret = process_madvise_one(pidfd, (void *)address, len,
				  MADV_POPULATE_READ);
	if (ret < 0) {
		perror("process_madvise MADV_POPULATE_READ");
		terminate_child(child);
		return 1;
	}
	usleep(250000);
	rss_populate = status_kib(child, "VmRSS:");
	swap_populate = status_kib(child, "VmSwap:");

	if (write(command_pipe[1], "c", 1) != 1 ||
	    read(report_pipe[0], &checksum, sizeof(checksum)) != sizeof(checksum)) {
		fprintf(stderr, "child checksum exchange failed\n");
		terminate_child(child);
		return 1;
	}
	for (i = 0; i < pages; i++)
		expected += i & 0xff;
	waitpid(child, &status, 0);

	printf("PAGEOUT rss=%ld KiB swap=%ld KiB; POPULATE rss=%ld KiB swap=%ld KiB\n",
	       rss_pageout, swap_pageout, rss_populate, swap_populate);
	if (!WIFEXITED(status) || WEXITSTATUS(status) || checksum != expected) {
		fprintf(stderr, "FAIL: target data changed during pageout/fault-in\n");
		return 1;
	}
	if (swap_populate >= swap_pageout || rss_populate <= rss_pageout) {
		fprintf(stderr, "FAIL: populate did not restore present PTEs\n");
		return 1;
	}

	puts("PASS: remote MADV_POPULATE_READ restored RSS and preserved data");
	return 0;
}
