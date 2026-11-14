# Makefile for VM server
.include <bsd.own.mk>

PROG=	vm
SRCS=	main.c alloc.c utility.c exit.c fork.c break.c \
	mmap.c slaballoc.c region.c pagefaults.c \
	rs.c pb.c regionavl.c \
	mem_anon.c mem_directphys.c mem_anon_contig.c mem_shared.c	\
	mem_cache.c cache.c vfs.c mem_file.c fdref.c acl.c

# The page table code: x86_64 has its own (4-level) implementation in
# arch/x86_64, the 2-level architectures share pagetable.c.
.if ${MACHINE_ARCH} == "x86_64"
SRCS+=	pagetable_x86_64.c
.else
SRCS+=	pagetable.c
.endif

.if ${MACHINE_ARCH} == "earm"
LDFLAGS+= -T ${.CURDIR}/arch/${MACHINE_ARCH}/vm.lds
.endif

.if ${MKPAE:Uno} != "no"
CPPFLAGS+= -DPAE=1
.endif

DPADD+=	${LIBSYS} ${LIBEXEC}
LDADD+=	-lsys -lexec

CPPFLAGS+= -I${.CURDIR} -I${.CURDIR}/arch/${MACHINE_ARCH}
CPPFLAGS+= -I${NETBSDSRCDIR}/minix

# For all other services, magic instrumentation involves instrumenting the
# libc malloc code, hooking its nested mmap/munmap calls, and ignoring its
# data. For VM, we need to do the exact opposite, since for VM, the malloc
# state is transferred as is. Thus, if the magic pass is enabled, tell it
# to skip the regular malloc instrumentation features.
MAGICFLAGS= -magic-disable-mem-functions -magic-disable-malloc-skip

.include "arch/${MACHINE_ARCH}/Makefile.inc"
.include <minix.service.mk>
