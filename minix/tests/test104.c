/*
 * test104: RLIMIT_CORE, the one resource limit PM keeps: default
 * (unlimited), a process that dies of SIGABRT leaves "core.<pid>" and
 * WCOREDUMP() unless its soft limit is zero ("ulimit -c 0"), the limit is
 * inherited across fork and exec, and the usual setrlimit rules hold (soft
 * above hard: EINVAL; only root raises the hard limit).
 */
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int max_error = 5;
#include "common.h"

/*
 * Fork a child that sets its soft core limit to 'soft' (unless negative),
 * optionally forks once more, and aborts.  Returns whether a core file was
 * made, checking that WCOREDUMP() agrees with the file's existence.
 */
static int
crash(int soft, int grandchild)
{
	struct rlimit rl;
	struct stat st;
	char name[32];
	pid_t pid, pid2;
	int status, fds[2], core, havefile;

	if (pipe(fds) != 0) e(100);

	pid = fork();
	if (pid < 0) e(101);
	if (pid == 0) {
		close(fds[0]);
		if (soft >= 0) {
			if (getrlimit(RLIMIT_CORE, &rl) != 0) _exit(1);
			rl.rlim_cur = soft;
			if (setrlimit(RLIMIT_CORE, &rl) != 0) _exit(2);
		}
		if (grandchild) {
			pid2 = fork();
			if (pid2 < 0) _exit(3);
			if (pid2 > 0) {
				/* Pass on the grandchild's pid and status. */
				if (waitpid(pid2, &status, 0) != pid2) _exit(4);
				write(fds[1], &pid2, sizeof(pid2));
				write(fds[1], &status, sizeof(status));
				_exit(0);
			}
		}
		signal(SIGABRT, SIG_DFL);
		abort();
		_exit(5);
	}
	close(fds[1]);
	if (waitpid(pid, &status, 0) != pid) e(102);
	if (grandchild) {
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(103);
		if (read(fds[0], &pid, sizeof(pid)) != sizeof(pid)) e(104);
		if (read(fds[0], &status, sizeof(status)) != sizeof(status))
			e(105);
	}
	close(fds[0]);

	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) e(106);
	core = !!WCOREDUMP(status);

	snprintf(name, sizeof(name), "core.%d", (int)pid);
	havefile = (stat(name, &st) == 0);
	if (havefile != core) e(107);
	if (havefile && st.st_size == 0) e(108);
	(void)unlink(name);

	return core;
}

static void
test_limits(void)
{
	struct rlimit rl, rl2;

	subtest = 1;

	memset(&rl, 0, sizeof(rl));
	if (getrlimit(RLIMIT_CORE, &rl) != 0) e(1);
	if (rl.rlim_cur != RLIM_INFINITY) e(2);
	if (rl.rlim_max != RLIM_INFINITY) e(3);

	/* The soft limit may not exceed the hard limit. */
	rl2.rlim_cur = 2000;
	rl2.rlim_max = 1000;
	if (setrlimit(RLIMIT_CORE, &rl2) != -1) e(4);
	if (errno != EINVAL) e(5);

	/* Lower and raise the soft limit; reading it back. */
	rl2.rlim_cur = 0;
	rl2.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &rl2) != 0) e(6);
	if (getrlimit(RLIMIT_CORE, &rl2) != 0) e(7);
	if (rl2.rlim_cur != 0 || rl2.rlim_max != RLIM_INFINITY) e(8);
	if (setrlimit(RLIMIT_CORE, &rl) != 0) e(9);
	if (getrlimit(RLIMIT_CORE, &rl2) != 0) e(10);
	if (rl2.rlim_cur != RLIM_INFINITY) e(11);
}

static void
test_hard(void)
{
	struct rlimit rl;
	pid_t pid;
	int status;

	subtest = 2;

	/* In a child: lowering the hard limit is final for non-root. */
	pid = fork();
	if (pid < 0) e(1);
	if (pid == 0) {
		rl.rlim_cur = 100;
		rl.rlim_max = 1000;
		if (setrlimit(RLIMIT_CORE, &rl) != 0) _exit(1);
		rl.rlim_cur = 1000;	/* up to the hard limit: fine */
		if (setrlimit(RLIMIT_CORE, &rl) != 0) _exit(2);
		rl.rlim_max = 2000;
		if (geteuid() == 0) {
			if (setrlimit(RLIMIT_CORE, &rl) != 0) _exit(3);
		} else {
			if (setrlimit(RLIMIT_CORE, &rl) != -1) _exit(4);
			if (errno != EPERM) _exit(5);
		}
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!WIFEXITED(status)) e(3);
	else if (WEXITSTATUS(status) != 0) e(10 + WEXITSTATUS(status));

	/* The child's limits were its own. */
	if (getrlimit(RLIMIT_CORE, &rl) != 0) e(4);
	if (rl.rlim_cur != RLIM_INFINITY || rl.rlim_max != RLIM_INFINITY) e(5);
}

static void
test_dump(void)
{
	subtest = 3;

	if (crash(-1, 0) != 1) e(1);	/* default: core file */
	if (crash(0, 0) != 0) e(2);	/* soft limit 0: none */
	if (crash(1, 0) != 1) e(3);	/* nonzero limits do not truncate */
	if (crash(0, 1) != 0) e(4);	/* inherited by fork */
	if (crash(-1, 1) != 1) e(5);
}

static void
test_exec(void)
{
	char cmd[128];
	pid_t pid;
	int status;

	subtest = 4;

	/* The limit survives exec: sh's ulimit reports it. */
	pid = fork();
	if (pid < 0) e(1);
	if (pid == 0) {
		struct rlimit rl;

		rl.rlim_cur = 0;
		rl.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &rl) != 0) _exit(1);
		snprintf(cmd, sizeof(cmd), "[ \"$(ulimit -c)\" = 0 ]");
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		_exit(2);
	}
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(3);

	/* And ulimit -c 0 in a shell stops its children's core files. */
	status = system("exec 2> /dev/null; ulimit -c 0; sh -c 'kill -ABRT $$'; "
	    "ls core.* > /dev/null 2>&1 && exit 1; exit 0");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(4);
	status = system("exec 2> /dev/null; sh -c 'kill -ABRT $$'; "
	    "ls core.* > /dev/null 2>&1 || exit 1; rm -f core.*");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(5);
}

int
main(void)
{
	start(104);

	test_limits();
	test_hard();
	test_dump();
	test_exec();

	quit();
	return 0;
}
