#include <sys/cdefs.h>
#include "namespace.h"

#include <errno.h>
#include <limits.h>
#include <sys/resource.h>
#include <string.h>
#include <unistd.h>
#include <lib.h>

/* Simple stub for now, except for RLIMIT_CORE, which PM keeps. */
int setrlimit(int resource, const struct rlimit *rlp)
{
	message m;

	if (resource == RLIMIT_CORE) {
		memset(&m, 0, sizeof(m));
		m.m_lc_pm_rlimit.set = 1;
		m.m_lc_pm_rlimit.resource = resource;
		m.m_lc_pm_rlimit.cur = rlp->rlim_cur;
		m.m_lc_pm_rlimit.max = rlp->rlim_max;
		return _syscall(PM_PROC_NR, PM_RLIMIT, &m);
	}

	switch (resource)
	{
		case RLIMIT_CPU:
		case RLIMIT_FSIZE:
		case RLIMIT_DATA:
		case RLIMIT_STACK:
		case RLIMIT_CORE:
		case RLIMIT_RSS:
		case RLIMIT_MEMLOCK:
		case RLIMIT_NPROC:
		case RLIMIT_NOFILE:
		case RLIMIT_SBSIZE:
		case RLIMIT_AS:
		/* case RLIMIT_VMEM: Same as RLIMIT_AS */
		case RLIMIT_NTHR:
			break;

		default:
			errno = EINVAL;
			return -1;
	}		

	return 0;
}
