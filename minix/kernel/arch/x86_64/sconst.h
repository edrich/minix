#ifndef __SCONST_H__
#define __SCONST_H__

#include "kernel/const.h"
#include "kernel/procoffsets.h"

/*
 * When the CPU traps from user mode it switches to the stack in tss.rsp0 and
 * pushes ss, rsp, rflags, cs and rip there: 5 quadwords.  tss.rsp0 points at
 * the two words reserved at the top of the kernel stack, the first of which
 * holds the pointer to the current process (see arch_finish_switch_to_user).
 * This is the offset to that pointer right after a trap without error code.
 */
#define CURR_PROC_PTR		40

/* Offsets within the frame pushed by the CPU. */
#define TRAP_RIP		0
#define TRAP_CS			8
#define TRAP_RFLAGS		16
#define TRAP_RSP		24
#define TRAP_SS			32

/*
 * tests whether the interrupt was triggered in kernel. If so, jump to the
 * label. Displacement tells the macro how far the frame pushed by the CPU
 * is from the current %rsp. The privilege level of the interrupted code is
 * in the lower 2 bits of its code segment selector.
 */
#define TEST_INT_IN_KERNEL(displ, label)	\
	testb	$RPL_MASK, (TRAP_CS + displ)(%rsp)	;\
	jz	label				;

/*
 * saves the basic interrupt context (no error code) to the process structure
 *
 * displ is the displacement of %rsp from the frame pushed by the CPU
 * pptr is the process structure pointer
 * tmp is an available temporary register
 */
#define SAVE_TRAP_CTX(displ, pptr, tmp)			\
	movq	(TRAP_RIP + displ)(%rsp), tmp		;\
	movq	tmp, PCREG(pptr)			;\
	movq	(TRAP_CS + displ)(%rsp), tmp		;\
	movq	tmp, CSREG(pptr)			;\
	movq	(TRAP_RFLAGS + displ)(%rsp), tmp	;\
	movq	tmp, PSWREG(pptr)			;\
	movq	(TRAP_RSP + displ)(%rsp), tmp		;\
	movq	tmp, SPREG(pptr)			;\
	movq	(TRAP_SS + displ)(%rsp), tmp		;\
	movq	tmp, SSREG(pptr)

/* all general purpose registers except %rbp (and %rsp) */
#define SAVE_GP_REGS(pptr)	\
	movq	%rax, AXREG(pptr)		;\
	movq	%rcx, CXREG(pptr)		;\
	movq	%rdx, DXREG(pptr)		;\
	movq	%rbx, BXREG(pptr)		;\
	movq	%rsi, SIREG(pptr)		;\
	movq	%rdi, DIREG(pptr)		;\
	movq	%r8, R8REG(pptr)		;\
	movq	%r9, R9REG(pptr)		;\
	movq	%r10, R10REG(pptr)		;\
	movq	%r11, R11REG(pptr)		;\
	movq	%r12, R12REG(pptr)		;\
	movq	%r13, R13REG(pptr)		;\
	movq	%r14, R14REG(pptr)		;\
	movq	%r15, R15REG(pptr)		;

#define RESTORE_GP_REGS(pptr)	\
	movq	AXREG(pptr), %rax		;\
	movq	CXREG(pptr), %rcx		;\
	movq	DXREG(pptr), %rdx		;\
	movq	BXREG(pptr), %rbx		;\
	movq	SIREG(pptr), %rsi		;\
	movq	DIREG(pptr), %rdi		;\
	movq	R8REG(pptr), %r8		;\
	movq	R9REG(pptr), %r9		;\
	movq	R10REG(pptr), %r10		;\
	movq	R11REG(pptr), %r11		;\
	movq	R12REG(pptr), %r12		;\
	movq	R13REG(pptr), %r13		;\
	movq	R14REG(pptr), %r14		;\
	movq	R15REG(pptr), %r15		;

/*
 * save the context of the interrupted process to the structure in the process
 * table. It pushes the %rbp to stack to get a scratch register. After %rsi is
 * saved, we can use it to get the saved %rbp from stack and save it to the
 * final location. On exit %rbp points to the process structure and %rsp is
 * where it was on entry.
 *
 * displ is the stack displacement. In case of an exception, there are two extra
 * value on the stack - error code and the exception number
 *
 * Unlike on i386 there are no segment registers to save or reload: the
 * kernel does not depend on the contents of %ds, %es, %fs or %gs.
 */
#define SAVE_PROCESS_CTX(displ, trapcode) \
								\
	cld /* set the direction flag to a known state */	;\
								\
	push	%rbp					;\
							;\
	movq	(CURR_PROC_PTR + 8 + displ)(%rsp), %rbp	;\
							\
	SAVE_GP_REGS(%rbp)				;\
	movl	$trapcode, P_KERN_TRAP_STYLE(%rbp)	;\
	pop	%rsi			/* get the orig %rbp and save it */ ;\
	movq	%rsi, BPREG(%rbp)			;\
							\
	SAVE_TRAP_CTX(displ, %rbp, %rsi)		;

/*
 * Once the context of a user process is saved, the frame pushed by the CPU
 * is no longer needed. Point %rsp at the top of the kernel stack again (at
 * the reserved words), which is aligned as the C calling convention wants it.
 */
#define RESET_K_STACK(displ)	\
	leaq	(CURR_PROC_PTR + displ)(%rsp), %rsp	;

/*
 * The registers a C function may clobber. Saved and restored around the
 * handling of an interrupt that arrived while the kernel was running (idle).
 * An odd number of registers, so that together with the 5 quadwords the CPU
 * pushed on a 16 byte aligned stack, the stack is aligned for a call again.
 */
#define PUSH_SCRATCH_REGS	\
	push	%rax	;\
	push	%rcx	;\
	push	%rdx	;\
	push	%rsi	;\
	push	%rdi	;\
	push	%r8	;\
	push	%r9	;\
	push	%r10	;\
	push	%r11	;

#define POP_SCRATCH_REGS	\
	pop	%r11	;\
	pop	%r10	;\
	pop	%r9	;\
	pop	%r8	;\
	pop	%rdi	;\
	pop	%rsi	;\
	pop	%rdx	;\
	pop	%rcx	;\
	pop	%rax	;

#define SCRATCH_REGS_SIZE	(9 * 8)

/* every general purpose register except %rsp: 15 of them */
#define PUSH_ALL_REGS	\
	PUSH_SCRATCH_REGS	\
	push	%rbx	;\
	push	%rbp	;\
	push	%r12	;\
	push	%r13	;\
	push	%r14	;\
	push	%r15	;

#define POP_ALL_REGS	\
	pop	%r15	;\
	pop	%r14	;\
	pop	%r13	;\
	pop	%r12	;\
	pop	%rbp	;\
	pop	%rbx	;\
	POP_SCRATCH_REGS

#define ALL_REGS_SIZE		(15 * 8)

/*
 * clear the IF flag in rflags which are stored somewhere in memory, e.g. on
 * stack. iret or popf will load the new value later
 */
#define CLEAR_IF(where)	\
	andq	$~0x200, where						;

#endif /* __SCONST_H__ */
