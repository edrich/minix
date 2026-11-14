/* eventfd.c - Linux-compatible eventfd for MINIX.
 *
 * Substrate: an AF_UNIX/SOCK_STREAM socketpair (the FIFO O_RDWR trick does
 * not exist on MINIX).  The public fd is the read end; the counter is what
 * is queued for it, in 8-byte units that each carry a value, written into
 * the libc-held other end.  So poll/select on the public fd work, and so
 * does a plain read(2): adding to the counter first takes back what is
 * pending and queues one unit with the sum, i.e. a read finds the whole
 * counter in a single unit (and repeated writes never fill the socket).
 * With EFD_SEMAPHORE the counter is that many units of value 1.
 *
 * A plain write(2) on the public fd - the usual way to post an eventfd on
 * Linux - would go into the other direction of the socketpair and be lost:
 * libc's write() asks __libc_eventfd_write() below first.  Descriptors are
 * found by number; one that was closed and whose number now names
 * something else is told apart by the socket's device number and, when
 * that was recycled too, by the write into the private end failing (EPIPE).
 * A dup()ed eventfd descriptor is not recognized by write(2): use
 * eventfd_write(), or the original descriptor.
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define MAX_EVENTFDS 64

static struct { int pub; int priv; int semaphore; dev_t dev; } efds[MAX_EVENTFDS];
static int nefds;

static int
_efd_find(int fd)
{
	int i;
	for (i = 0; i < nefds; i++)
		if (efds[i].pub == fd) return i;
	return -1;
}

int
eventfd(unsigned int initval, int flags)
{
	int fds[2], i;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
		return -1;

	/* fd values get recycled by VFS after close(2): purge a stale entry
	 * for this public fd (and close its libc-held peer), or the peer end
	 * would be a dead socket.  The stale peer is still open, so it cannot
	 * collide with the fresh pair. */
	{
		int j = _efd_find(fds[0]);
		if (j >= 0) {
			close(efds[j].priv);
			efds[j] = efds[--nefds];
		}
	}

	if (nefds >= MAX_EVENTFDS) {
		close(fds[0]);
		close(fds[1]);
		errno = ENFILE;
		return -1;
	}

	i = nefds++;
	efds[i].pub = fds[0];
	efds[i].priv = fds[1];
	efds[i].semaphore = (flags & EFD_SEMAPHORE) != 0;
	efds[i].dev = 0;
	{
		struct stat st;
		if (fstat(fds[0], &st) == 0)
			efds[i].dev = st.st_dev;
	}

	if (flags & EFD_NONBLOCK) {
		fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
		fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
	}
	if (flags & EFD_CLOEXEC)
		fcntl(fds[0], F_SETFD, fcntl(fds[0], F_GETFD) | FD_CLOEXEC);
	/* the libc-held end must never leak into exec'd programs, and
	 * writing it after the public end was closed must not raise SIGPIPE */
	fcntl(fds[1], F_SETFD, fcntl(fds[1], F_GETFD) | FD_CLOEXEC);
	fcntl(fds[1], F_SETNOSIGPIPE, 1);

	if (initval > 0 && eventfd_write(fds[0], initval) != 0) {
		int e = errno;
		nefds--;	/* entry i is the last one */
		close(fds[0]);
		close(fds[1]);
		errno = e;
		return -1;
	}
	return fds[0];
}

/*
 * Take everything that is pending on 'fd' without blocking and return the
 * sum of the units' values in '*sum'.  A unit could in theory be split by
 * a short stream transfer: its remainder is coming, wait for it.
 */
static int
_efd_drain(int fd, uint64_t *sum)
{
	char buf[64 * 8];
	uint64_t u;
	size_t have = 0, k;
	ssize_t r;
	int serrno = errno;

	*sum = 0;
	for (;;) {
		r = recv(fd, buf + have, sizeof(buf) - have,
		    have % 8 ? 0 : MSG_DONTWAIT);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				return -1;
			break;
		}
		if (r == 0)
			break;
		have += r;
		for (k = 0; k + 8 <= have; k += 8) {
			memcpy(&u, buf + k, 8);
			*sum += u;
		}
		memmove(buf, buf + k, have - k);
		have -= k;
	}
	errno = serrno;
	return 0;
}

/* Add 'val' to the counter of eventfd slot 'i'. */
static int
_efd_add(int i, uint64_t val)
{
	static const uint64_t one = 1;
	uint64_t pending;

	if (val == UINT64_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (efds[i].semaphore) {
		for (; val > 0; val--)
			if (write(efds[i].priv, &one, 8) != 8)
				return -1;
		return 0;
	}
	if (_efd_drain(efds[i].pub, &pending) != 0)
		return -1;
	if (val > UINT64_MAX - 1 - pending)
		val = UINT64_MAX - 1 - pending;	/* Linux would block */
	val += pending;
	if (val > 0 && write(efds[i].priv, &val, 8) != 8)
		return -1;
	return 0;
}

int
eventfd_write(int fd, eventfd_t val)
{
	int i;

	if ((i = _efd_find(fd)) < 0) { errno = EBADF; return -1; }
	/* If the public end was closed, the write fails with EPIPE (the
	 * private end is F_SETNOSIGPIPE, so no signal); Linux says EIO. */
	if (_efd_add(i, val) != 0) {
		if (errno == EPIPE) errno = EIO;
		return -1;
	}
	return 0;
}

/*
 * write(2) on a descriptor: called by libc's write().  Sets '*handled'
 * and returns write()'s result if 'fd' is an eventfd; otherwise the caller
 * goes on with the real write.
 */
ssize_t
__libc_eventfd_write(int fd, const void *buf, size_t nbytes, int *handled)
{
	struct stat st;
	uint64_t val;
	int i;

	*handled = 0;
	if (nefds == 0 || (i = _efd_find(fd)) < 0)
		return 0;
	if (fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode) ||
	    st.st_dev != efds[i].dev)
		goto stale;

	if (nbytes < 8) {
		*handled = 1;
		errno = EINVAL;
		return -1;
	}
	memcpy(&val, buf, 8);
	if (_efd_add(i, val) != 0) {
		if (errno == EPIPE)
			goto stale;	/* our public end is gone */
		*handled = 1;
		return -1;
	}
	*handled = 1;
	return 8;

stale:
	/* the number was closed and names something else now */
	close(efds[i].priv);
	efds[i] = efds[--nefds];
	return 0;
}

int
eventfd_read(int fd, eventfd_t *val)
{
	uint64_t t, more = 0;
	ssize_t r;
	int sem = 0;

	r = _efd_find(fd);
	if (r >= 0) sem = efds[r].semaphore;

	r = read(fd, &t, 8);
	if (r != 8) {
		if (r < 0) return -1;	/* EAGAIN when empty + nonblock */
		errno = EBADF;		/* cannot happen: units are 8B */
		return -1;
	}
	/*
	 * Not a semaphore: the counter is everything that is pending
	 * (normally the one unit just read; more if writers raced).  This
	 * must never block, even on a blocking eventfd.
	 */
	if (!sem && _efd_drain(fd, &more) != 0)
		return -1;
	*val = t + more;
	return 0;
}
