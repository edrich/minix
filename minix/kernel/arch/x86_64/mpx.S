/* This file is part of the lowest layer of the MINIX kernel.  (The other part
 * is "proc.c".)  The lowest layer does process switching and message handling.
 * It contains the interrupt, exception and trap entry points for x86_64 long
 * mode.
 *
 * Kernel is entered either because of kernel-calls, ipc-calls, interrupts or
 * exceptions. TSS is set so that the kernel stack is loaded. The user context is
 * saved to the proc table and the handler of the event is called. Once the
 * handler is done, switch_to_user() function is called to pick a new process,
 * finish what needs to be done for the next process to run, sets its context
 * and switch to userspace.
 *
 * Stack discipline: on a trap from user mode the CPU pushes five quadwords
 * at tss.rsp0, which is 16 byte aligned.  Once the context is saved in the
 * process structure that frame is dead, and the entry code resets %rsp to
 * tss.rsp0 (RESET_K_STACK) so that C code is called with an aligned stack.
 * On a trap from kernel mode the CPU aligns the stack itself before pushing.
 */

#include "kernel/kernel.h" /* configures the kernel */

/* sections */

#include <machine/vm.h>
#include "kernel/kernel.h"
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ipcconst.h>
#include <minix/com.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include "archconst.h"
#include "kernel/const.h"
#include "kernel/proc.h"
#include "sconst.h"
#include <machine/multiboot.h>

#include "arch_proto.h" /* K_STACK_SIZE */

IMPORT(copr_not_available_handler)
IMPORT(switch_to_user)

.text
/*===========================================================================*/
/*				interrupt handlers			     */
/*===========================================================================*/

#define PIC_IRQ_HANDLER(irq)	\
	movl	$irq, %edi						    	;\
	call	_C_LABEL(irq_handle)	/* intr_handle(irq_handlers[irq]) */	;

#define EOI_MASTER	\
	movb	$END_OF_INT, %al					;\
	outb	$INT_CTL	/* reenable interrupts in master pic */	;

#define EOI_SLAVE	\
	movb	$END_OF_INT, %al					;\
	outb	$INT_CTL	/* reenable interrupts in master pic */	;\
	outb	$INT2_CTL	/* reenable slave 8259		  */	;

/*
 * A hardware interrupt arrives either while a process runs, in which case
 * we save its context and go through the scheduler afterwards, or while the
 * kernel is idle (halted with interrupts enabled), in which case we return
 * to where we were, with interrupts disabled.
 */
/* Note this is a macro, it just looks like a subroutine. */
#define hwint(irq, EOI) \
	TEST_INT_IN_KERNEL(0, 0f)					;\
									\
	SAVE_PROCESS_CTX(0, KTS_INT_HARD)				;\
	RESET_K_STACK(0)						;\
	movq	%rbp, %rdi						;\
	xorl	%ebp, %ebp	/* for stack trace */			;\
	call	_C_LABEL(context_stop)					;\
	PIC_IRQ_HANDLER(irq)						;\
	EOI								;\
	jmp	_C_LABEL(switch_to_user)				;\
									\
0:									\
	PUSH_SCRATCH_REGS						;\
	cld								;\
	call	_C_LABEL(context_stop_idle)				;\
	PIC_IRQ_HANDLER(irq)						;\
	EOI								;\
	CLEAR_IF((SCRATCH_REGS_SIZE + TRAP_RFLAGS)(%rsp))		;\
	POP_SCRATCH_REGS						;\
	iretq								;

/* Each of these entry points is an expansion of the hwint macro */
ENTRY(hwint00)
/* Interrupt routine for irq 0 (the clock). */
	hwint(0, EOI_MASTER)

ENTRY(hwint01)
/* Interrupt routine for irq 1 (keyboard) */
	hwint(1, EOI_MASTER)

ENTRY(hwint02)
/* Interrupt routine for irq 2 (cascade!) */
	hwint(2, EOI_MASTER)

ENTRY(hwint03)
/* Interrupt routine for irq 3 (second serial) */
	hwint(3, EOI_MASTER)

ENTRY(hwint04)
/* Interrupt routine for irq 4 (first serial) */
	hwint(4, EOI_MASTER)

ENTRY(hwint05)
/* Interrupt routine for irq 5 (XT winchester) */
	hwint(5, EOI_MASTER)

ENTRY(hwint06)
/* Interrupt routine for irq 6 (floppy) */
	hwint(6, EOI_MASTER)

ENTRY(hwint07)
/* Interrupt routine for irq 7 (printer) */
	hwint(7, EOI_MASTER)

ENTRY(hwint08)
/* Interrupt routine for irq 8 (realtime clock) */
	hwint(8, EOI_SLAVE)

ENTRY(hwint09)
/* Interrupt routine for irq 9 (irq 2 redirected) */
	hwint(9, EOI_SLAVE)

ENTRY(hwint10)
/* Interrupt routine for irq 10 */
	hwint(10, EOI_SLAVE)

ENTRY(hwint11)
/* Interrupt routine for irq 11 */
	hwint(11, EOI_SLAVE)

ENTRY(hwint12)
/* Interrupt routine for irq 12 */
	hwint(12, EOI_SLAVE)

ENTRY(hwint13)
/* Interrupt routine for irq 13 (FPU exception) */
	hwint(13, EOI_SLAVE)

ENTRY(hwint14)
/* Interrupt routine for irq 14 (AT winchester) */
	hwint(14, EOI_SLAVE)

ENTRY(hwint15)
/* Interrupt routine for irq 15 */
	hwint(15, EOI_SLAVE)

/*
 * IPC is only from a process to kernel
 */
ENTRY(ipc_entry_softint_orig)
	SAVE_PROCESS_CTX(0, KTS_INT_ORIG)
	jmp ipc_entry_common

ENTRY(ipc_entry_softint_um)
	SAVE_PROCESS_CTX(0, KTS_INT_UM)
	jmp ipc_entry_common

ENTRY(ipc_entry_common)
	RESET_K_STACK(0)

	/* keep the pointer to the current process in a register that
	 * survives function calls */
	movq	%rbp, %rbx

	/* stop user process cycles */
	movq	%rbp, %rdi
	/* for stack trace */
	xorl	%ebp, %ebp
	call	_C_LABEL(context_stop)

	/*
	 * pass the syscall arguments from userspace to the handler; they
	 * are in the saved context: do_ipc(call nr, src_dest, message).
	 * See <machine/ipcconst.h> for the register conventions.
	 */
	movq	DXREG(%rbx), %rdi
	movq	DIREG(%rbx), %rsi
	movq	SIREG(%rbx), %rdx
	call	_C_LABEL(do_ipc)

	/* save the return value */
	movq    %rax, AXREG(%rbx)

	jmp	_C_LABEL(switch_to_user)


/*
 * kernel call is only from a process to kernel
 */
ENTRY(kernel_call_entry_orig)
	SAVE_PROCESS_CTX(0, KTS_INT_ORIG)
	jmp	kernel_call_entry_common

ENTRY(kernel_call_entry_um)
	SAVE_PROCESS_CTX(0, KTS_INT_UM)
	jmp	kernel_call_entry_common

ENTRY(kernel_call_entry_common)
	RESET_K_STACK(0)
	movq	%rbp, %rbx

	/* stop user process cycles */
	movq	%rbp, %rdi
	/* for stack trace */
	xorl	%ebp, %ebp
	call	_C_LABEL(context_stop)

	/* kernel_call(message pointer from userspace, caller) */
	movq	DIREG(%rbx), %rdi
	movq	%rbx, %rsi
	call	_C_LABEL(kernel_call)

	jmp	_C_LABEL(switch_to_user)


.balign 16
/*
 * called by the exception interrupt vectors. If the exception does not push
 * errorcode, we assume that the vector handler pushed 0 instead. Next pushed
 * thing is the vector number. From this point on we can continue as if every
 * exception pushes an error code
 */
exception_entry:
	/*
	 * check if it is a nested trap by looking at the privilege level
	 * of the saved code segment selector
	 */
	TEST_INT_IN_KERNEL(16, exception_entry_nested)

exception_entry_from_user:
	SAVE_PROCESS_CTX(16, KTS_INT_HARD)

	/*
	 * keep a pointer to the vector number pushed by the vector handler
	 * and the interrupt state pushed by the cpu (struct
	 * exception_frame), and align the stack: 7 quadwords were pushed.
	 */
	movq	%rsp, %r12
	subq	$8, %rsp

	/* stop user process cycles */
	movq	%rbp, %rdi
	/* for stack trace clear %rbp */
	xorl	%ebp, %ebp
	call	_C_LABEL(context_stop)

	xorl	%edi, %edi	/* it's not a nested exception */
	movq	%r12, %rsi
	call 	_C_LABEL(exception_handler)

	jmp	_C_LABEL(switch_to_user)

exception_entry_nested:
	/* 7 quadwords on an aligned stack, plus these 15: aligned again */
	PUSH_ALL_REGS
	cld
	movl	$1, %edi			/* it's a nested exception */
	leaq	ALL_REGS_SIZE(%rsp), %rsi
	call	_C_LABEL(exception_handler)
	POP_ALL_REGS

	/* clear the error code and the exception number */
	addq	$16, %rsp
	/* resume execution at the point of exception */
	iretq

ENTRY(restore_user_context_int)
	movq	%rdi, %rbp

	/* reconstruct the stack for iret */
	push	$USER_DS_SELECTOR	/* ss */
	push	SPREG(%rbp)
	push	PSWREG(%rbp)
	push	$USER_CS_SELECTOR	/* cs */
	push	PCREG(%rbp)

	/* Restore the general-purpose registers. */
	RESTORE_GP_REGS(%rbp)

	movq	BPREG(%rbp), %rbp

	iretq	/* continue process */

/*===========================================================================*/
/*				exception handlers			     */
/*===========================================================================*/

#define EXCEPTION_ERR_CODE(vector)	\
	push	$vector			;\
	jmp	exception_entry

#define EXCEPTION_NO_ERR_CODE(vector)	\
	push	$0		;\
	EXCEPTION_ERR_CODE(vector)

LABEL(divide_error)
	EXCEPTION_NO_ERR_CODE(DIVIDE_VECTOR)

LABEL(single_step_exception)
	EXCEPTION_NO_ERR_CODE(DEBUG_VECTOR)

LABEL(nmi)
	EXCEPTION_NO_ERR_CODE(NMI_VECTOR)

LABEL(breakpoint_exception)
	EXCEPTION_NO_ERR_CODE(BREAKPOINT_VECTOR)

LABEL(overflow)
	EXCEPTION_NO_ERR_CODE(OVERFLOW_VECTOR)

LABEL(bounds_check)
	EXCEPTION_NO_ERR_CODE(BOUNDS_VECTOR)

LABEL(inval_opcode)
	EXCEPTION_NO_ERR_CODE(INVAL_OP_VECTOR)

LABEL(copr_not_available)
	TEST_INT_IN_KERNEL(0, copr_not_available_in_kernel)
	SAVE_PROCESS_CTX(0, KTS_INT_HARD)
	RESET_K_STACK(0)
	/* stop user process cycles */
	movq	%rbp, %rdi
	xorl	%ebp, %ebp
	call	_C_LABEL(context_stop)
	call	_C_LABEL(copr_not_available_handler)
	/* reached upon failure only */
	jmp	_C_LABEL(switch_to_user)

copr_not_available_in_kernel:
	push	$0
	push	$COPROC_NOT_VECTOR
	jmp	exception_entry_nested

LABEL(double_fault)
	EXCEPTION_ERR_CODE(DOUBLE_FAULT_VECTOR)

LABEL(copr_seg_overrun)
	EXCEPTION_NO_ERR_CODE(COPROC_SEG_VECTOR)

LABEL(inval_tss)
	EXCEPTION_ERR_CODE(INVAL_TSS_VECTOR)

LABEL(segment_not_present)
	EXCEPTION_ERR_CODE(SEG_NOT_VECTOR)

LABEL(stack_exception)
	EXCEPTION_ERR_CODE(STACK_FAULT_VECTOR)

LABEL(general_protection)
	EXCEPTION_ERR_CODE(PROTECTION_VECTOR)

LABEL(page_fault)
	EXCEPTION_ERR_CODE(PAGE_FAULT_VECTOR)

LABEL(copr_error)
	EXCEPTION_NO_ERR_CODE(COPROC_ERR_VECTOR)

LABEL(alignment_check)
	EXCEPTION_ERR_CODE(ALIGNMENT_CHECK_VECTOR)

LABEL(machine_check)
	EXCEPTION_NO_ERR_CODE(MACHINE_CHECK_VECTOR)

LABEL(simd_exception)
	EXCEPTION_NO_ERR_CODE(SIMD_EXCEPTION_VECTOR)

/*===========================================================================*/
/*				reload_cr3				     */
/*===========================================================================*/
/* PUBLIC void reload_cr3(void); */
ENTRY(reload_cr3)
	movq	%cr3, %rax
	movq	%rax, %cr3
	ret

/*===========================================================================*/
/*				data					     */
/*===========================================================================*/

.data
.short	0x526F	/* this must be the first data entry (magic #) */

.bss
.balign 16
k_initial_stack:
.space	K_STACK_SIZE
LABEL(__k_unpaged_k_initial_stktop)

/*
 * the kernel stack
 */
k_boot_stack:
.space	K_STACK_SIZE	/* kernel stack */ /* FIXME use macro here */
LABEL(k_boot_stktop)	/* top of kernel stack */

/* stack for double faults, so that a kernel stack overflow can be reported */
.space	K_STACK_SIZE
LABEL(k_df_stktop)

.balign K_STACK_SIZE
LABEL(k_stacks_start)

/* two pages for each stack, one for data, other as a sandbox */
.space	2 * (K_STACK_SIZE * CONFIG_MAX_CPUS)

LABEL(k_stacks_end)

/* top of kernel stack */
