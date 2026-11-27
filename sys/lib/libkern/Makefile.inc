#	MINIX: builds the minimal libkern (see Makefile) for the boot code, in
#	the style of ../libz/Makefile.inc.
#
#	S	must be set to the top of the 'sys' tree.
#	KERNDST	may be set to the location of the directory where library
#		objects are to be built.  Defaults to ${.OBJDIR}/lib/kern.
#	KERNMISCCPPFLAGS, KERNMISCMAKEFLAGS
#		Miscellaneous cpp and make flags for the library's Makefile.

KERNDST?=	${.OBJDIR}/lib/kern
KERNDOTDIR?=	../../.

KERNDIR=	${S:S@^.@${KERNDOTDIR}@:Q}/lib/libkern
KERNLIB=	${KERNDST}/libkern.a

KERNMAKE= \
	cd ${KERNDST} && ${MAKE} -f ${KERNDIR:Q}/Makefile \
	    KERNDIR=${KERNDIR:Q} \
	    CC=${CC:Q} CFLAGS=${CFLAGS:Q} CPUFLAGS= \
	    AS=${AS:Q} AFLAGS=${AFLAGS:Q} \
	    LORDER=${LORDER:Q} \
	    TSORT=${TSORT:Q} \
	    LD=${LD:Q} STRIP=${STRIP:Q} \
	    AR=${AR:Q} NM=${NM:Q} \
	    RANLIB=${RANLIB:Q} SIZE=${SIZE:Q} \
	    MACHINE=${MACHINE} MACHINE_ARCH=${MACHINE_ARCH:Q} \
	    KERNCPPFLAGS=${CPPFLAGS:S@^-I.@-I${KERNDOTDIR}@g:Q} \
	    KERNMISCCPPFLAGS=${KERNMISCCPPFLAGS:Q} \
	    ${KERNMISCMAKEFLAGS}

${KERNLIB}:		.NOTMAIN .MAKE __always_make_kernlib
	@echo making sure the kern library is up to date...
	@${KERNMAKE} libkern.a
	@echo done

clean:			.NOTMAIN cleankernlib
cleankernlib:		.NOTMAIN
	@echo cleaning the kern library objects
	@if [ -d "${KERNDST}" ]; then ${KERNMAKE} clean; fi
	@echo done

cleandir distclean:	.NOTMAIN cleandirkernlib
cleandirkernlib:	.NOTMAIN
	@echo cleandiring the kern library objects
	@if [ -d "${KERNDST}" ]; then ${KERNMAKE} cleandir; fi
	@echo done

dependall depend:	.NOTMAIN dependkernlib
dependkernlib:		.NOTMAIN .MAKE __always_make_kernlib
	@echo depending the kern library objects
	@${KERNMAKE} depend
	@echo done

__always_make_kernlib:	.NOTMAIN
	@mkdir -p ${KERNDST}

.PHONY: __always_make_kernlib
.PHONY: cleankernlib cleandirkernlib dependkernlib
