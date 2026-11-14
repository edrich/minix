#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <unistd.h>
#include <string.h>

/*
 * PM keeps one id per process for both the process group and the session
 * (mp_procgrp), so this is the getsid(2) call: the id of process 'p', or
 * of the caller when 'p' is zero; ESRCH for a process that does not exist.
 */
pid_t getpgid(pid_t p)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_getsid.pid = p;
  return(_syscall(PM_PROC_NR, PM_GETSID, &m));
}
