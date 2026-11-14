/*
 * test105: getpgid(2).  PM has one id for process group and session, so
 * getpgid(0) and getpgid(getpid()) equal getpgrp(), a forked child is in
 * its parent's group until it calls setsid(), after which the parent sees
 * the child's pid as the child's group, and a pid that does not exist
 * yields ESRCH.
 */
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int max_error = 5;
#include "common.h"

int
main(void)
{
	pid_t pid, pg;
	int status, up[2], down[2];
	char c;

	start(105);

	pg = getpgrp();
	if (pg <= 0) e(1);
	if (getpgid(0) != pg) e(2);
	if (getpgid(getpid()) != pg) e(3);

	if (pipe(up) != 0 || pipe(down) != 0) e(4);
	pid = fork();
	if (pid < 0) e(5);
	if (pid == 0) {
		close(up[0]);
		close(down[1]);
		if (getpgid(0) != pg) _exit(1);
		if (getpgid(getppid()) != pg) _exit(2);
		if (write(up[1], "a", 1) != 1) _exit(3);
		if (read(down[0], &c, 1) != 1) _exit(4);
		if (setsid() != getpid()) _exit(5);
		if (getpgid(0) != getpid()) _exit(6);
		if (getpgrp() != getpid()) _exit(7);
		if (write(up[1], "b", 1) != 1) _exit(8);
		if (read(down[0], &c, 1) != 1) _exit(9);
		_exit(0);
	}
	close(up[1]);
	close(down[0]);

	if (read(up[0], &c, 1) != 1 || c != 'a') e(6);
	if (getpgid(pid) != pg) e(7);		/* still in our group */
	if (write(down[1], "x", 1) != 1) e(8);
	if (read(up[0], &c, 1) != 1 || c != 'b') e(9);
	if (getpgid(pid) != pid) e(10);		/* its own group now */
	if (getpgid(0) != pg) e(11);		/* ours is unchanged */
	if (write(down[1], "x", 1) != 1) e(12);

	if (waitpid(pid, &status, 0) != pid) e(13);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(14);

	/* the child is gone now */
	errno = 0;
	if (getpgid(pid) != -1 || errno != ESRCH) e(15);

	quit();
	return 0;	/* not reached */
}
