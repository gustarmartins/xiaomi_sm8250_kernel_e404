// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../kselftest.h"
#include "cgroup_util.h"

/*
 * Android 4.19 carries PSI together with the stable cgroup_file_ctx
 * backport.  A partial backport once stored the trigger in of->priv instead
 * of ctx->psi.trigger, corrupting cgroup_file_release() on close.  Exercise
 * the full open/write/poll/close lifetime repeatedly so that combination
 * cannot silently regress.
 */
int main(void)
{
	static const char trigger[] = "some 50000 1000000";
	char root[PATH_MAX];
	char pressure[PATH_MAX];
	int i;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");
	if (snprintf(pressure, sizeof(pressure), "%s/memory.pressure", root) >=
	    sizeof(pressure))
		ksft_exit_fail_msg("cgroup path is too long\n");

	for (i = 0; i < 100; i++) {
		struct pollfd pfd = { .events = POLLPRI };
		ssize_t written;

		pfd.fd = open(pressure, O_RDWR | O_CLOEXEC | O_NONBLOCK);
		if (pfd.fd < 0) {
			if (errno == ENOENT || errno == EOPNOTSUPP)
				ksft_exit_skip("per-cgroup PSI isn't available\n");
			ksft_exit_fail_msg("open memory.pressure: %s\n",
					   strerror(errno));
		}
		written = write(pfd.fd, trigger, sizeof(trigger));
		if (written != sizeof(trigger)) {
			int saved_errno = errno;

			close(pfd.fd);
			ksft_exit_fail_msg("write PSI trigger: %s\n",
					   strerror(saved_errno));
		}
		if (poll(&pfd, 1, 0) < 0) {
			int saved_errno = errno;

			close(pfd.fd);
			ksft_exit_fail_msg("poll PSI trigger: %s\n",
					   strerror(saved_errno));
		}
		if (close(pfd.fd))
			ksft_exit_fail_msg("close PSI trigger: %s\n",
					   strerror(errno));
	}

	ksft_test_result_pass("cgroup PSI trigger lifetime\n");
	return KSFT_PASS;
}
