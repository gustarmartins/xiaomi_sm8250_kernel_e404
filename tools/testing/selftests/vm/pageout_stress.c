// SPDX-License-Identifier: GPL-2.0
/*
 * pageout_stress - stress process_madvise(MADV_PAGEOUT) against
 * disposable children while racing exit, fork, SIGSTOP/SIGCONT and optional
 * memory-cgroup charge migration.
 *
 * K1 harness for the munch-mm-diagnostic kernel: the 2026-07-22 panic
 * dereferenced LIST_POISON1 in shrink_page_list() during a cryoproc
 * MADV_PAGEOUT batch. This tool drives the same kernel path at high
 * iteration counts so CONFIG_DEBUG_LIST/DEBUG_VM can catch the
 * corrupting site. Run once with MGLRU disabled and once with MGLRU
 * 0x0003 (echo to /sys/kernel/mm/lru_gen/enabled) and capture each
 * separately with mm-diag-evidence.sh.
 *
 * ESRCH/EINVAL/ENOMEM from the raced iterations are expected and
 * counted, not failures; the pass criterion is kernel health (no
 * panic, WARN, list corruption or unexplained OOM in dmesg/pstore).
 *
 * Usage: pageout_stress [-i iterations] [-c children] [-m mib]
 *			 [-g memcg-root]
 *   -i  total PAGEOUT iterations (default 10000, the K1 gate)
 *   -c  concurrent disposable children (default 4)
 *   -m  anon MiB dirtied per child (default 64)
 *   -g  cgroup-v1 memory root used for required memcg-move races
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

#define CHUNK_IOVECS 64

static long iterations = 10000;
static int nchildren = 4;
static size_t child_mib = 64;
static const char *memcg_root;
static char memcg_a[PATH_MAX], memcg_b[PATH_MAX];
static char memcg_a_procs[PATH_MAX], memcg_b_procs[PATH_MAX];

static long stat_ok, stat_bytes, stat_err[64];
static long stat_memcg_ok, stat_memcg_err;

struct child {
	pid_t pid;
	int pidfd;
	void *base;   /* child-reported mapping base */
	size_t len;
};

static int pidfd_open(pid_t pid)
{
	return (int)syscall(__NR_pidfd_open, pid, 0);
}

static ssize_t pageout(int pidfd, void *base, size_t len)
{
	struct iovec iov[CHUNK_IOVECS];
	size_t chunk = len / CHUNK_IOVECS;
	int i;

	for (i = 0; i < CHUNK_IOVECS; i++) {
		iov[i].iov_base = (char *)base + (size_t)i * chunk;
		iov[i].iov_len = chunk;
	}
	return syscall(__NR_process_madvise, pidfd, iov,
		       (unsigned long)CHUNK_IOVECS, MADV_PAGEOUT, 0);
}

static int write_text(const char *path, const char *text)
{
	ssize_t len = (ssize_t)strlen(text);
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	if (write(fd, text, len) != len) {
		int saved = errno;

		close(fd);
		errno = saved ? saved : EIO;
		return -errno;
	}
	close(fd);
	return 0;
}

static int move_to_memcg(pid_t pid, const char *procs)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", pid);
	return write_text(procs, buf);
}

static void cleanup_memcgs(void);

static int setup_memcgs(void)
{
	char move_charge[PATH_MAX];

	if (!memcg_root)
		return 0;
	snprintf(memcg_a, sizeof(memcg_a), "%s/pageout-stress-%d-a",
		 memcg_root, getpid());
	snprintf(memcg_b, sizeof(memcg_b), "%s/pageout-stress-%d-b",
		 memcg_root, getpid());
	if (mkdir(memcg_a, 0755)) {
		perror("mkdir memcg");
		return -errno;
	}
	if (mkdir(memcg_b, 0755)) {
		perror("mkdir memcg");
		cleanup_memcgs();
		return -errno;
	}
	snprintf(memcg_a_procs, sizeof(memcg_a_procs), "%s/cgroup.procs",
		 memcg_a);
	snprintf(memcg_b_procs, sizeof(memcg_b_procs), "%s/cgroup.procs",
		 memcg_b);

	/*
	 * Move both anonymous and file charges with the task. This makes the
	 * migration overlap the same page and memcg lifetime paths exercised by
	 * process_madvise instead of moving only future allocations.
	 */
	snprintf(move_charge, sizeof(move_charge),
		 "%s/memory.move_charge_at_immigrate", memcg_a);
	if (write_text(move_charge, "3")) {
		perror("enable charge migration in memcg A");
		cleanup_memcgs();
		return -errno;
	}
	snprintf(move_charge, sizeof(move_charge),
		 "%s/memory.move_charge_at_immigrate", memcg_b);
	if (write_text(move_charge, "3")) {
		perror("enable charge migration in memcg B");
		cleanup_memcgs();
		return -errno;
	}
	return 0;
}

static void cleanup_memcgs(void)
{
	if (!memcg_root)
		return;
	if (rmdir(memcg_a))
		perror("rmdir memcg A");
	if (rmdir(memcg_b))
		perror("rmdir memcg B");
}

/* Child: dirty anon memory, report base, keep some pages warm forever. */
static void child_main(int wfd)
{
	size_t len = child_mib << 20;
	volatile char *p;
	size_t i;


	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		_exit(1);
	for (i = 0; i < len; i += 4096)
		p[i] = (char)i;
	if (write(wfd, &p, sizeof(p)) != sizeof(p))
		_exit(1);
	close(wfd);
	for (unsigned int n = 0;; n++) {
		/* touch 1/16 of the range to force refaults after pageout */
		for (i = 0; i < len / 16; i += 4096)
			p[i]++;
		/*
		 * Periodically fork a short-lived grandchild: COW briefly
		 * raises mapcounts on the very pages being paged out,
		 * exercising the mapcount!=1 skip and isolation races.
		 */
		if (n % 10 == 9) {
			pid_t g = fork();

			if (g == 0)
				_exit(0);
			if (g > 0)
				waitpid(g, NULL, 0);
		}
		usleep(20000);
	}
}

static void spawn_child(struct child *c)
{
	int pfd[2];

	if (pipe(pfd))
		exit(1);
	c->len = child_mib << 20;
	c->pid = fork();
	if (c->pid == 0) {
		close(pfd[0]);
		child_main(pfd[1]);
		_exit(0);
	}
	close(pfd[1]);
	if (read(pfd[0], &c->base, sizeof(c->base)) != sizeof(c->base))
		exit(1);
	close(pfd[0]);
	c->pidfd = pidfd_open(c->pid);
}

static void reap_child(struct child *c)
{
	kill(c->pid, SIGKILL);
	waitpid(c->pid, NULL, 0);
	if (c->pidfd >= 0)
		close(c->pidfd);
}

struct race_arg {
	pid_t pid;
	int mode;      /* 1 = SIGKILL, 2 = STOP/CONT, 3 = memcg move */
	unsigned int delay_us;
	const char *memcg_procs;
	int result;
};

static void *race_thread(void *arg)
{
	struct race_arg *ra = arg;

	usleep(ra->delay_us);
	if (ra->mode == 1) {
		kill(ra->pid, SIGKILL);
		ra->result = 0;
	} else if (ra->mode == 2) {
		kill(ra->pid, SIGSTOP);
		usleep(1000);
		kill(ra->pid, SIGCONT);
		ra->result = 0;
	} else {
		ra->result = move_to_memcg(ra->pid, ra->memcg_procs);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	struct child *kids;
	long it;
	int opt, i, spawned = 0, rc = 0;

	while ((opt = getopt(argc, argv, "i:c:m:g:")) != -1) {
		switch (opt) {
		case 'i':
			iterations = atol(optarg);
			break;
		case 'c':
			nchildren = atoi(optarg);
			break;
		case 'm':
			child_mib = (size_t)atol(optarg);
			break;
		case 'g':
			memcg_root = optarg;
			break;
		default:
			fprintf(stderr,
				"usage: %s [-i n] [-c n] [-m mib] [-g memcg-root]\n",
				argv[0]);
			return 2;
		}
	}

	kids = calloc(nchildren, sizeof(*kids));
	if (!kids)
		return 2;
	if (setup_memcgs())
		return 2;
	for (i = 0; i < nchildren; i++) {
		spawn_child(&kids[i]);
		spawned++;
		if (memcg_root && move_to_memcg(kids[i].pid, memcg_a_procs)) {
			perror("place child in memcg A");
			rc = 2;
			goto out;
		}
	}

	srandom(getpid());
	for (it = 0; it < iterations; it++) {
		struct child *c = &kids[it % nchildren];
		int mode = it % 8;
		pthread_t th;
		struct race_arg ra;
		int have_racer = 0;
		ssize_t ret;

		/*
		 * Modes 0-4, 7: plain pageout (bulk of the 10k gate); the
		 * children fork short-lived grandchildren on their own,
		 * so fork/COW races run continuously underneath.
		 * Mode 5: child SIGKILLed mid-pageout (exit race).
		 * Mode 6: child stopped/continued mid-pageout (freezer-ish).
		 * Mode 7: migrate task and existing charges between memcgs.
		 */
		if (mode == 5 || mode == 6 || (mode == 7 && memcg_root)) {
			ra.pid = c->pid;
			ra.mode = mode == 5 ? 1 : mode == 6 ? 2 : 3;
			ra.delay_us = random() % 3000;
			ra.memcg_procs = ((it / 8) & 1) ?
				memcg_a_procs : memcg_b_procs;
			ra.result = -1;
			if (!pthread_create(&th, NULL, race_thread, &ra))
				have_racer = 1;
		}

		ret = pageout(c->pidfd, c->base, c->len);
		if (ret >= 0) {
			stat_ok++;
			stat_bytes += ret;
		} else if (errno < 64) {
			stat_err[errno]++;
		}

		if (have_racer) {
			pthread_join(th, NULL);
			if (ra.mode == 3) {
				if (ra.result)
					stat_memcg_err++;
				else
					stat_memcg_ok++;
			}
		}
		if (mode == 5) {
			/* child may be dead; respawn to keep pressure up */
			reap_child(c);
			spawn_child(c);
			if (memcg_root &&
			    move_to_memcg(c->pid, memcg_a_procs)) {
				perror("replace child in memcg A");
				rc = 2;
				goto out;
			}
		}
		if ((it + 1) % 500 == 0) {
			printf("[%ld/%ld] ok=%ld bytes=%ldM memcg=%ld/%ld",
			       it + 1, iterations, stat_ok, stat_bytes >> 20,
			       stat_memcg_ok, stat_memcg_err);
			printf(" errs:");
			for (i = 0; i < 64; i++)
				if (stat_err[i])
					printf(" %s=%ld", strerror(i),
					       stat_err[i]);
			printf("\n");
			fflush(stdout);
		}
	}

out:
	for (i = 0; i < spawned; i++)
		reap_child(&kids[i]);
	cleanup_memcgs();
	if (rc)
		return rc;
	printf("DONE iterations=%ld ok=%ld bytes=%ldM memcg=%ld/%ld\n",
	       iterations, stat_ok, stat_bytes >> 20,
	       stat_memcg_ok, stat_memcg_err);
	if (!stat_ok || !stat_bytes) {
		fprintf(stderr,
			"FAIL: process_madvise never reclaimed a positive byte count\n");
		return 1;
	}
	if (memcg_root && !stat_memcg_ok) {
		fprintf(stderr, "FAIL: no memcg charge migration succeeded\n");
		return 1;
	}
	printf("Now check dmesg/pstore via mm-diag-evidence.sh post <run>\n");
	return 0;
}
