/*	getrlimit                             Author: Erik van der Kouwe
 *      query resource consumtion limits      4 December 2009
 *
 * Based on these specifications:
 * http://www.opengroup.org/onlinepubs/007908775/xsh/getdtablesize.html 
 * http://www.opengroup.org/onlinepubs/007908775/xsh/getrlimit.html 
 */
 
#include <sys/cdefs.h>
#include "namespace.h"

#include <errno.h>
#include <limits.h>
#include <sys/resource.h>
#include <string.h>
#include <unistd.h>
#include <lib.h>

int getrlimit(int resource, struct rlimit *rlp)
{
	rlim_t limit;
	message m;

	/* The core file limit is kept by PM (0 = no core dumps). */
	if (resource == RLIMIT_CORE) {
		memset(&m, 0, sizeof(m));
		m.m_lc_pm_rlimit.resource = resource;
		if (_syscall(PM_PROC_NR, PM_RLIMIT, &m) < 0)
			return -1;
		rlp->rlim_cur = m.m_lc_pm_rlimit.cur;
		rlp->rlim_max = m.m_lc_pm_rlimit.max;
		return 0;
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
		case RLIMIT_SBSIZE:
		case RLIMIT_AS:
		/* case RLIMIT_VMEM: Same as RLIMIT_AS */
		case RLIMIT_NTHR:
			/* no limit enforced (however architectural limits 
			 * may apply) 
			 */	
			limit = RLIM_INFINITY;
			break;

		case RLIMIT_NPROC:
			limit = CHILD_MAX;
			break;

		case RLIMIT_NOFILE:
			limit = OPEN_MAX;
			break;

		default:
			errno = EINVAL;
			return -1;
	}		

	/* return limit */
	rlp->rlim_cur = limit;
	rlp->rlim_max = limit;
	return 0;
}

