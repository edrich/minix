#include <minix/ipcconst.h>
#include <machine/asm.h>

/* int ipc_minix_kerninfo(struct minix_kerninfo **ptr) */
ENTRY(ipc_minix_kerninfo)
	push	%rdi		/* save the return struct pointer */
	xorl	%edi, %edi
	xorl	%esi, %esi
	movl	$MINIX_KERNINFO, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	pop	%rcx		/* rcx = return struct ptr */
	movq	%rdx, (%rcx)
	ret
