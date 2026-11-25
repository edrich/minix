/* amd64 signal-context validation and faults while returning to user mode. */
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__x86_64__)
static volatile sig_atomic_t mode, handled, failed;

static void
handler(int signo, int code, struct sigcontext *sc)
{
	struct sigcontext bad = *sc;

	(void)signo;
	(void)code;
	switch (mode) {
	case 0: bad.sc_magic = 0; break;
	case 1: bad.trap_style = 0; break;
	case 2: bad.trap_style = -1; break;
	case 3: bad.trap_style = INT_MAX; break;
	default: _exit(2);
	}
	/* A rejected context must leave the original register state intact so
	 * the handler can return normally through the original signal frame.
	 */
	errno = 0;
	if (sigreturn(&bad) != -1 || errno != EINVAL)
		failed = 1;
	handled++;
}

static void
bad_stack(void)
{
	/* No stack access is needed to enter the IPC trap. The invalid call
	 * returns immediately, forcing an IRET with a noncanonical user RSP.
	 */
	__asm__ volatile(
	    "movabsq $0x0000800000000000, %%rsp\n\t"
	    "xorl %%edx, %%edx\n\t"
	    "int $33\n\t"
	    "ud2" : : : "rdx", "memory");
	__builtin_unreachable();
}

static void
bad_pc_handler(int signo, int code, struct sigcontext *sc)
{
	(void)signo;
	(void)code;
	sc->sc_rip = 0x0000800000000000L;
}

static int
fault_child(int bad_pc)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		if (bad_pc) {
			if (signal(SIGUSR1, (void (*)(int))bad_pc_handler) == SIG_ERR)
				_exit(3);
			kill(getpid(), SIGUSR1);
			_exit(4);
		}
		bad_stack();
	}
	if (waitpid(pid, &status, 0) != pid)
		return 1;
	return !WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV;
}
#endif

int
main(void)
{
#if defined(__x86_64__)
	struct rlimit limit = { 0, 0 };
	int i;

	/* This test needs no scratch directory or external utilities, so it
	 * can also run in the minimal boot ramdisk before a full root exists.
	 */
	printf("Test 106: amd64 return-to-user validation\n");
	fflush(stdout);
	if (setrlimit(RLIMIT_CORE, &limit) != 0)
		return 1;
	alarm(30);
	if (fault_child(0) || fault_child(1)) {
		printf("Test 106: invalid return did not produce SIGSEGV\n");
		return 1;
	}
	if (signal(SIGUSR1, (void (*)(int))handler) == SIG_ERR)
		return 1;
	for (i = 0; i < 4; i++) {
		mode = i;
		if (kill(getpid(), SIGUSR1) != 0 || handled != i + 1 || failed) {
			printf("Test 106: signal context check %d failed\n", i);
			return 1;
		}
	}
	alarm(0);
	printf("Test 106: ok\n");
#else
	printf("Test 106: skipped (amd64 only)\n");
#endif
	return 0;
}
