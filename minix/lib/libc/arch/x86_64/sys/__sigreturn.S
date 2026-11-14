/* This routine is the low-level code for returning from signals.   */
/* It calls _sigreturn, which is the normal "system call" routine. */
/* Both __sigreturn and _sigreturn are needed. */
#include <machine/asm.h>

IMPORT(sigreturn)
ENTRY(__sigreturn)
	/* The signal handler returned here through sf_ra_sigreturn of
	 * struct sigframe_sigcontext.  Skip the fake frame (sf_fp, sf_ra)
	 * and pick up the context pointer (sf_scpcopy) as the argument.
	 * The kernel aligned the frame such that the stack is as sigreturn
	 * expects it on entry.
	 */
	addq	$16, %rsp
	pop	%rdi
#ifndef __PIC__
	jmp	_C_LABEL(sigreturn)
#else
	jmp	PIC_PLT(_C_LABEL(sigreturn))
#endif /* PIC */
