#	MINIX: a minimal libkern for the x86 boot code when it is built for
#	amd64 (with -m32).  On i386 the boot code links the system's libminc
#	instead, which on amd64 is 64-bit code.  The sources are the ones libminc
#	uses for these functions.

LIB=	kern
LIBISPRIVATE=	yes

COMMONDIR:=	${.PARSEDIR}/../../../common/lib/libc
BUILTINSDIR:=	${.PARSEDIR}/../../external/bsd/compiler_rt/dist/lib/builtins

CPPFLAGS+=	${KERNCPPFLAGS} ${KERNMISCCPPFLAGS}
CPPFLAGS+=	-D_STANDALONE -I${.PARSEDIR}/../..

.PATH.c: ${COMMONDIR}/string ${COMMONDIR}/stdlib ${BUILTINSDIR}

SRCS=	memchr.c memcmp.c memcpy.c memmove.c memset.c \
	strcat.c strchr.c strcmp.c strcpy.c strlcpy.c strlen.c \
	strncmp.c strncpy.c strrchr.c strtoul.c \
	udivdi3.c udivmoddi4.c umoddi3.c

# CFLAGS comes from the parent make's command line, so add these here.
CPPFLAGS+=	-Wno-missing-prototypes -Wno-cast-qual

.undef DESTDIR
.include <bsd.lib.mk>
