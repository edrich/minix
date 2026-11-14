# Makefile for arch-dependent kernel code
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:	${HERE}

# The kernel lives in the top 2GB of the address space.  It must not use the
# red zone below the stack pointer, which interrupts and exceptions taken in
# kernel mode would overwrite, nor the SSE/x87 registers, which hold the
# state of whichever process used them last.
KERNEL_COPTS=	-mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx -msoft-float
CFLAGS+=	${KERNEL_COPTS}
AFLAGS+=	-mcmodel=kernel

# Code shared with libc, libsys, libminc, libexec and libtimers is compiled
# again for the kernel rather than taken from those libraries, as it has to
# be built with the options above.
.PATH: ${NETBSDSRCDIR}/minix/lib/libc/arch/${MACHINE_ARCH}
.PATH: ${NETBSDSRCDIR}/minix/lib/libc/gen
.PATH: ${NETBSDSRCDIR}/minix/lib/libsys
.PATH: ${NETBSDSRCDIR}/minix/lib/libsys/arch/${MACHINE_ARCH}
.PATH: ${NETBSDSRCDIR}/minix/lib/libminc
.PATH: ${NETBSDSRCDIR}/minix/lib/libexec
.PATH: ${NETBSDSRCDIR}/minix/lib/libtimers
.PATH: ${NETBSDSRCDIR}/sys/lib/libsa
.PATH: ${NETBSDSRCDIR}/common/lib/libc/arch/${MACHINE_ARCH}/string
.PATH: ${NETBSDSRCDIR}/common/lib/libc/string
.PATH: ${NETBSDSRCDIR}/common/lib/libc/stdlib
.PATH: ${NETBSDSRCDIR}/lib/libc/string
.PATH: ${NETBSDSRCDIR}/lib/libc/stdlib

SRCS+=	_cpufeature.c _cpuid.S get_bp.S \
	assert.c stacktrace.c \
	atoi.c printf.c subr_prf.c \
	strcmp.S strcpy.S strlen.S strncmp.c \
	memcpy.S memmove.S memset.S
SRCS+=	strcat.S strncpy.c strlcpy.c strlcat.c \
	read_tsc.S read_tsc_64.c get_randomness.c cpuavg.c \
	tmrs_clr.c tmrs_exp.c tmrs_set.c \
	exec_elf.c

# LSC: putchar and kputc have the same signature. A bit hackish.
CPPFLAGS.subr_prf.c+= -Dputchar=kputc

# Activate optional support, may be deactivated.
CPPFLAGS.subr_prf.c+= -DLIBSA_PRINTF_LONGLONG_SUPPORT -DLIBSA_PRINTF_WIDTH_SUPPORT

# objects we want unpaged from the above
MINLIB_OBJS_UNPAGED= _cpufeature.o _cpuid.o get_bp.o
SYS_OBJS_UNPAGED=assert.o stacktrace.o
MINC_OBJS_UNPAGED= atoi.o \
	printf.o subr_prf.o \
	strcmp.o strcpy.o strlen.o strncmp.o \
	memcpy.o memmove.o memset.o

# some object files we give a symbol prefix (or namespace) of __k_unpaged_
# that must live in their own unique namespace.  They run before the kernel
# is mapped at its link address, at the address it was loaded at, which
# works because the kernel code model can reach both the top and the bottom
# 2GB of the address space.
#
.for unpaged_obj in head.o pre_init.o direct_tty_utils.o \
	pg_utils.o klib.o utility.o arch_reset.o \
	${MINLIB_OBJS_UNPAGED} ${MINC_OBJS_UNPAGED} ${SYS_OBJS_UNPAGED}
unpaged_${unpaged_obj}: ${unpaged_obj}
	${OBJCOPY} --prefix-symbols=__k_unpaged_ ${.OBJDIR}/${unpaged_obj} $@
UNPAGED_OBJS += unpaged_${unpaged_obj}
ORIG_UNPAGED_OBJS += ${unpaged_obj}
.endfor

CLEANFILES+= ${UNPAGED_OBJS} head.o pre_init.o

# head.S and pre_init.c only exist in the unpaged namespace, so they are not
# in SRCS; have their dependencies tracked nevertheless.
DPSRCS+= head.S pre_init.c

SRCS+=	mpx.S arch_clock.c arch_do_vmctl.c arch_system.c \
	klib.S memory.c protect.c direct_tty_utils.c arch_reset.c pg_utils.c

SRCS+=	do_iopenable.c do_readbios.c do_sdevio.c exception.c i8259.c \
	usermapped_data_arch.c \
	usermapped_glo_ipc.S

OBJS.kernel+=	${UNPAGED_OBJS}

.if ${USE_DEBUGREG} != "no"
SRCS+=  breakpoints.c
.endif

klib.o mpx.o head.o: procoffsets.h

SRCS+= procoffsets.h

PROCOFFSETSCF=procoffsets.cf

.PATH: ${NETBSDSRCDIR}/minix/include/arch/${MACHINE_ARCH}/include

procoffsets.h: ${PROCOFFSETSCF} kernel.h proc.h stackframe.h archtypes.h
	${_MKTARGET_CREATE}
	${TOOL_CAT} ${HERE}/${PROCOFFSETSCF} | \
		${TOOL_GENASSYM} -- ${CC} ${CFLAGS:N-Wa,*} \
		${CPPFLAGS} ${PROF} \
		${GENASSYM_CPPFLAGS} >$@.tmp && \
	  mv -f $@.tmp $@

sconst.h: procoffsets.h
head.o klib.o mpx.o: sconst.h

# Multiboot loaders, QEMU's -kernel among them, only load 32-bit ELF
# kernels.  The kernel's first instructions are 32-bit code that switches to
# long mode, so all that is needed is to repackage the linked kernel: same
# contents and load addresses, 32-bit ELF headers.
realall: kernel.elf64
kernel.elf64: kernel
	${_MKTARGET_CREATE}
	cp kernel kernel.elf64
	${OBJCOPY} -O elf32-i386 kernel.elf64 kernel
CLEANFILES+= kernel.elf64
