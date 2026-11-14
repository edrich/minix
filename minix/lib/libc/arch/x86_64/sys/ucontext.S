#include <machine/asm.h>
#include <ucontextoffsets.h>

IMPORT(getuctx)
IMPORT(setuctx)
IMPORT(resumecontext)

	.globl	_C_LABEL(__errno)

/* int getcontext(ucontext_t *ucp)
 *	Initialise the structure pointed to by ucp to the current user context
 *	of the calling thread. */
ENTRY(getcontext)
ENTRY(_getcontext)
	/* In case a process does not use the FPU and is neither interested in
	 * saving its signal mask, then we can skip the context switch to
	 * PM and kernel altogether and only save general-purpose registers. */

	/* Check null pointer */
	testq %rdi, %rdi		/* ucp == NULL? */
	jnz 3f				/* Not null, continue */
	subq $8, %rsp			/* align the stack for the call */
	call PIC_PLT(_C_LABEL(__errno))
	addq $8, %rsp
	movl $EFAULT, (%rax)
	movl $-1, %eax			/* return -1 */
	ret

3:	/* Check flags */
	movl UC_FLAGS(%rdi), %eax	/* eax = ucp->uc_flags */
	andl $(_UC_IGNFPU|_UC_IGNSIGM), %eax
	cmpl $(_UC_IGNFPU|_UC_IGNSIGM), %eax
	jz 5f				/* Ignore both, skip getuctx */
	push %rdi			/* save ucp (and align the stack) */
	call PIC_PLT(_C_LABEL(getuctx))	/* getuctx(ucp) */
	pop %rdi			/* restore ucp */

5:
	/* Save the context */
	pop %rax			/* rax = real return address */
	movq %rax, PC(%rdi)		/* Save it in the mcp struct */
	movq %rsp, SP(%rdi)		/* Save stack pointer, as it is after
					 * getcontext returns */

	/* Save the callee-saved registers.  The argument registers are
	 * saved as well: they have no meaningful value on return from
	 * getcontext, but setcontext loads them, as that is how
	 * makecontext passes arguments to a new context. */
	movq %rbx, BX(%rdi)
	movq %rbp, BP(%rdi)
	movq %r12, R12(%rdi)
	movq %r13, R13(%rdi)
	movq %r14, R14(%rdi)
	movq %r15, R15(%rdi)
	movq %rdi, DI(%rdi)
	movq %rsi, SI(%rdi)
	movq %rdx, DX(%rdi)
	movq %rcx, CX(%rdi)
	movq %r8, R8(%rdi)
	movq %r9, R9(%rdi)
	movl $MCF_MAGIC, MAGIC(%rdi)	/* Set magic value */

	movq %rax, %rdx
	xorl %eax, %eax			/* Return 0 */
	jmp *%rdx  			/* Return to the return address */


/* int setcontext(const ucontext_t *ucp)
 *	Restore the user context pointed to by ucp. A successful call to
 *	setcontext does not return; program execution resumes at the point
 *	specified by the ucp argument. If ucp was created with getcontext(),
 *	program execution continues as if the corresponding call of getcontext()
 *	had just returned. If ucp was created with makecontext(), program
 *	execution continues with the function passed to makecontext(). */
ENTRY(setcontext)
	/* In case a process does not use the FPU and is neither interested in
	 * restoring its signal mask, then we can skip the context switch to
	 * PM and kernel altogether and restore state here. */

	/* Check null pointer */
	testq %rdi, %rdi		/* ucp == NULL? */
	jnz 3f				/* Not null, continue */
	movl $EFAULT, %edx
0:	push %rdx			/* preserve errno (and align stack) */
	call PIC_PLT(_C_LABEL(__errno))
	pop %rdx
	movl %edx, (%rax)
	movl $-1, %eax			/* return -1 */
	ret

3:	/* Check flags */
	cmpl $MCF_MAGIC, MAGIC(%rdi)	/* is the magic value set (is context valid)?*/
	jz 4f				/* is set, proceed */
	movl $EINVAL, %edx		/* not set, return error code */
	jmp 0b

4:	movl UC_FLAGS(%rdi), %eax	/* eax = ucp->uc_flags */
	andl $(_UC_IGNFPU|_UC_IGNSIGM), %eax
	cmpl $(_UC_IGNFPU|_UC_IGNSIGM), %eax
	jz 5f			/* Ignore both, so don't bother restoring FPU
				 * state and signal mask */

	push %rdi			/* save ucp (and align the stack) */
	call PIC_PLT(_C_LABEL(setuctx))	/* setuctx(ucp) */
	pop %rdi			/* restore ucp */

5:	/* Restore the registers */
	movq %rdi, %r11			/* r11 = ucp */
	movq BX(%r11), %rbx
	movq BP(%r11), %rbp
	movq R12(%r11), %r12
	movq R13(%r11), %r13
	movq R14(%r11), %r14
	movq R15(%r11), %r15
	movq DI(%r11), %rdi
	movq SI(%r11), %rsi
	movq DX(%r11), %rdx
	movq CX(%r11), %rcx
	movq R8(%r11), %r8
	movq R9(%r11), %r9
	movq SP(%r11), %rsp		/* Restore stack pointer */

	xorl %eax, %eax			/* Return 0 */
	jmp *PC(%r11)  			/* Return to RTA */


/* void ctx_start(void)
 *	A wrapper to start function `func' of a context created by
 *	makecontext().  makecontext() arranged for the context to start
 *	here with:
 *	  rbx           = func
 *	  r12           = ucp
 *	  rdi .. r9     = the first six arguments of func
 *	  (rsp) ...     = the remaining arguments, rsp aligned to 16 bytes
 *	which is exactly the state required just before a call of func.
 *	rbx and r12 are callee-saved, so ucp survives the call.  When func
 *	returns, resumecontext starts the next context in the linked list
 *	(or exits the program if there is none).
 */
ENTRY(ctx_start)
	call *%rbx			/* func(arg1, ..., argn) */
	movq %r12, %rdi
	call PIC_PLT(_C_LABEL(resumecontext))	/* resumecontext(ucp) */
	hlt				/* never reached */
