# $NetBSD: Makefile.inc,v 1.1 2010/08/07 18:01:34 joerg Exp $

CPPFLAGS+=	-DELFSIZE=64

# MINIX: x86_64 is dual-stack like i386: HAVE_INITFINI_ARRAY makes crt0 walk
# .preinit_array/.init_array/.fini_array, and crt0-common.c additionally
# retains the legacy extern _init/_fini calls (crti.S + crtbegin.S' .ctor
# walk) for in-tree-compiler binaries. See crt0-common.c.
CPPFLAGS+=	-DHAVE_INITFINI_ARRAY
