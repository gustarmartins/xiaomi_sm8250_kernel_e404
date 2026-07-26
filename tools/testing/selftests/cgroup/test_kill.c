// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest.h"
#include "cgroup_util.h"

#define TEST_TIMEOUT_LOOPS	2000
#define TEST_TIMEOUT_US		5000

static int cg_proc_count(const char *cgroup)
{
	char buf[PAGE_SIZE];
	char *p;
	int count = 0;

	if (cg_read(cgroup, "cgroup.procs", buf, sizeof(buf)))
		return -1;

	for (p = buf; *p; p++)
		if (*p == '\n')
			count++;

	return count;
}

static int cg_wait_for_proc_count(const char *cgroup, int expected)
{
	int i;

	for (i = 0; i < TEST_TIMEOUT_LOOPS; i++) {
		if (cg_proc_count(cgroup) == expected)
			return 0;
		usleep(TEST_TIMEOUT_US);
	}

	return -1;
}

static int cg_wait_for_min_proc_count(const char *cgroup, int expected)
{
	int i;

	for (i = 0; i < TEST_TIMEOUT_LOOPS; i++) {
		if (cg_proc_count(cgroup) >= expected)
			return 0;
		usleep(TEST_TIMEOUT_US);
	}

	return -1;
}

static int child_pause(const char *cgroup, void *arg)
{
	for (;;)
		pause();

	return 0;
}

static void reap_or_kill(pid_t pid)
{
	int status;

	if (pid <= 0)
		return;

	if (waitpid(pid, &status, WNOHANG) == 0) {
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	}
}

static int test_cgkill_simple(const char *root)
{
	pid_t pids[32];
	char *cgroup = NULL;
	int ret = KSFT_FAIL;
	int i;

	for (i = 0; i < ARRAY_SIZE(pids); i++)
		pids[i] = -1;

	cgroup = cg_name(root, "cg_kill_simple");
	if (!cgroup || cg_create(cgroup))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(pids); i++) {
		pids[i] = cg_run_nowait(cgroup, child_pause, NULL);
		if (pids[i] < 0)
			goto cleanup;
	}

	if (cg_wait_for_proc_count(cgroup, ARRAY_SIZE(pids)))
		goto cleanup;
	if (cg_write(cgroup, "cgroup.kill", "1"))
		goto cleanup;
	if (cg_wait_for_proc_count(cgroup, 0))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (i = 0; i < ARRAY_SIZE(pids); i++)
		reap_or_kill(pids[i]);
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

static int test_cgkill_tree(const char *root)
{
	char *groups[5] = { NULL };
	pid_t pids[3] = { -1, -1, -1 };
	int ret = KSFT_FAIL;
	int i;

	groups[0] = cg_name(root, "cg_kill_tree");
	if (!groups[0])
		goto cleanup;
	groups[1] = cg_name(groups[0], "a");
	groups[2] = cg_name(groups[1], "b");
	groups[3] = cg_name(groups[0], "c");
	groups[4] = cg_name(groups[3], "d");

	for (i = 1; i < ARRAY_SIZE(groups); i++)
		if (!groups[i])
			goto cleanup;
	for (i = 0; i < ARRAY_SIZE(groups); i++)
		if (cg_create(groups[i]))
			goto cleanup;

	pids[0] = cg_run_nowait(groups[0], child_pause, NULL);
	pids[1] = cg_run_nowait(groups[2], child_pause, NULL);
	pids[2] = cg_run_nowait(groups[4], child_pause, NULL);
	if (pids[0] < 0 || pids[1] < 0 || pids[2] < 0)
		goto cleanup;

	if (cg_wait_for_proc_count(groups[0], 1) ||
	    cg_wait_for_proc_count(groups[2], 1) ||
	    cg_wait_for_proc_count(groups[4], 1))
		goto cleanup;
	if (cg_write(groups[0], "cgroup.kill", "1"))
		goto cleanup;
	if (cg_wait_for_proc_count(groups[0], 0) ||
	    cg_wait_for_proc_count(groups[2], 0) ||
	    cg_wait_for_proc_count(groups[4], 0))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (i = 0; i < ARRAY_SIZE(pids); i++)
		reap_or_kill(pids[i]);
	for (i = ARRAY_SIZE(groups) - 1; i >= 0; i--) {
		if (groups[i])
			cg_destroy(groups[i]);
		free(groups[i]);
	}
	return ret;
}

static int fork_racer(const char *cgroup, void *arg)
{
	for (;;) {
		pid_t pid = fork();

		if (pid == 0)
			child_pause(cgroup, arg);
		if (pid < 0)
			usleep(5000);
		else
			usleep(500);
	}

	return 0;
}

static int test_cgkill_fork_race(const char *root)
{
	char *cgroup = NULL;
	pid_t pid = -1;
	int ret = KSFT_FAIL;

	cgroup = cg_name(root, "cg_kill_fork_race");
	if (!cgroup || cg_create(cgroup))
		goto cleanup;

	pid = cg_run_nowait(cgroup, fork_racer, NULL);
	if (pid < 0)
		goto cleanup;
	if (cg_wait_for_min_proc_count(cgroup, 16))
		goto cleanup;

	if (cg_write(cgroup, "cgroup.kill", "1"))
		goto cleanup;
	if (cg_wait_for_proc_count(cgroup, 0))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	reap_or_kill(pid);
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

static int test_cgkill_rejects_invalid(const char *root)
{
	char *cgroup = NULL;
	int ret = KSFT_FAIL;

	cgroup = cg_name(root, "cg_kill_invalid");
	if (!cgroup || cg_create(cgroup))
		goto cleanup;

	errno = 0;
	if (!cg_write(cgroup, "cgroup.kill", "0") || errno != ERANGE)
		goto cleanup;
	errno = 0;
	if (!cg_write(cgroup, "cgroup.kill", "2") || errno != ERANGE)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

static int test_cgkill_rejects_threaded(const char *root)
{
	char *cgroup = NULL;
	int ret = KSFT_FAIL;

	cgroup = cg_name(root, "cg_kill_threaded");
	if (!cgroup || cg_create(cgroup))
		goto cleanup;
	if (cg_write(cgroup, "cgroup.type", "threaded"))
		goto cleanup;

	errno = 0;
	if (!cg_write(cgroup, "cgroup.kill", "1") || errno != EOPNOTSUPP)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

#define T(x) { x, #x }
static const struct {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cgkill_simple),
	T(test_cgkill_tree),
	T(test_cgkill_fork_race),
	T(test_cgkill_rejects_invalid),
	T(test_cgkill_rejects_threaded),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int ret = EXIT_SUCCESS;
	int i;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");
	if (access(root, W_OK))
		ksft_exit_skip("cgroup v2 root isn't writable\n");

	ksft_print_header();

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ret = EXIT_FAILURE;
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	return ret;
}
