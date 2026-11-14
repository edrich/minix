#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <unistd.h>

/*
 * A write on an eventfd (eventfd.c: emulated in libc) adds to its counter.
 * Weak: static programs that do not use eventfd do not pull it in.
 */
ssize_t __libc_eventfd_write(int, const void *, size_t, int *) __weak;

ssize_t write(int fd, const void *buffer, size_t nbytes)
{
  message m;

  if (__libc_eventfd_write != NULL) {
	int handled;
	ssize_t r = __libc_eventfd_write(fd, buffer, nbytes, &handled);

	if (handled)
		return r;
  }

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readwrite.fd = fd;
  m.m_lc_vfs_readwrite.len = nbytes;
  m.m_lc_vfs_readwrite.buf = (vir_bytes)buffer;
  m.m_lc_vfs_readwrite.cum_io = 0;	/* reserved for future use */
  return(_syscall(VFS_PROC_NR, VFS_WRITE, &m));
}

