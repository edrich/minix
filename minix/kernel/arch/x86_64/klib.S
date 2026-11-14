/* sections */

#include <minix/config.h>
#include <minix/const.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include <machine/vm.h>
#include "archconst.h"
#include "kernel/const.h"
#include "sconst.h"
#include <machine/multiboot.h>

/*
 * This file contains a number of assembly code utility routines needed by the
 * kernel.  They follow the System V AMD64 calling convention: arguments in
 * %rdi, %rsi, %rdx, %rcx, %r8, %r9; result in %rax.
 */

ENTRY(__main)
	ret


/*===========================================================================*/
/*				phys_insw				     */
/*===========================================================================*/
/*
 * PUBLIC void phys_insw(Port_t port, phys_bytes buf, size_t count);
 * Input an array from an I/O port.  Absolute address version of insw().
 */
/* transfer data from (disk controller) port to memory */
ENTRY(phys_insw)
	cld
	movq	%rdx, %rcx	/* byte count */
	movl	%edi, %edx	/* port to read from */
	movq	%rsi, %rdi	/* destination addr */
	shrq	$1, %rcx	/* word count */
	rep insw	/* input many words */
	ret


/*===========================================================================*/
/*				phys_insb				     */
/*===========================================================================*/
/*
 * PUBLIC void phys_insb(Port_t port, phys_bytes buf, size_t count);
 * Input an array from an I/O port.  Absolute address version of insb().
 */
/* transfer data from (disk controller) port to memory byte by byte */
ENTRY(phys_insb)
	cld
	movq	%rdx, %rcx	/* byte count */
	movl	%edi, %edx	/* port to read from */
	movq	%rsi, %rdi	/* destination addr */
	rep insb	/* input many bytes */
	ret


/*===========================================================================*/
/*				phys_outsw				     */
/*===========================================================================*/
/*
 * PUBLIC void phys_outsw(Port_t port, phys_bytes buf, size_t count);
 * Output an array to an I/O port.  Absolute address version of outsw().
 */
/* transfer data from memory to (disk controller) port */
ENTRY(phys_outsw)
	cld
	movq	%rdx, %rcx	/* byte count */
	movl	%edi, %edx	/* port to write to */
	/* source addr is in %rsi already */
	shrq	$1, %rcx	/* word count */
	rep outsw	/* output many words */
	ret


/*===========================================================================*/
/*				phys_outsb				     */
/*===========================================================================*/
/*
 * PUBLIC void phys_outsb(Port_t port, phys_bytes buf, size_t count);
 * Output an array to an I/O port.  Absolute address version of outsb().
 */
/* transfer data from memory to (disk controller) port byte by byte */
ENTRY(phys_outsb)
	cld
	movq	%rdx, %rcx	/* byte count */
	movl	%edi, %edx	/* port to write to */
	/* source addr is in %rsi already */
	rep outsb	/* output many bytes */
	ret


/*===========================================================================*/
/*				phys_copy				     */
/*===========================================================================*/
/*
 * PUBLIC phys_bytes phys_copy(phys_bytes source, phys_bytes destination,
 *			phys_bytes bytecount);
 * Copy a block of data.  Despite the name, the addresses are (kernel)
 * virtual addresses, as on i386; use the direct map to get at physical
 * memory.  Returns 0, or the faulting address if a page fault was caught
 * (see PHYS_COPY_CATCH).
 */
ENTRY(phys_copy)
	cld
	xchgq	%rdi, %rsi	/* %rsi = source, %rdi = destination */
	movq	%rdx, %rcx
	shrq	$3, %rcx	/* count of quadwords */
	rep 	movsq
	movq	%rdx, %rcx
	andq	$7, %rcx	/* remainder */
	rep 	movsb
	xorl	%eax, %eax		/* 0 means: no fault */
LABEL(phys_copy_fault)		/* kernel can send us here */
	ret

LABEL(phys_copy_fault_in_kernel)	/* kernel can send us here */
	movq	%cr2, %rax
	ret


/*===========================================================================*/
/*				copy_msg_from_user			     */
/*===========================================================================*/
/*
 * int copy_msg_from_user(message * user_mbuf, message * dst);
 *
 * Copies a message from user process space to a kernel buffer. This
 * function assumes that the process address space is installed (cr3 loaded).
 *
 * This function from the callers point of view either succeeds or returns an
 * error which gives the caller a chance to respond accordingly. In fact it
 * either succeeds or if it generates a pagefault, general protection or other
 * exception, the trap handler has to redirect the execution to
 * __user_copy_msg_pointer_failure where the error is reported to the caller
 * without resolving the pagefault. It is not kernel's problem to deal with
 * wrong pointers from userspace and the caller should return an error to
 * userspace as if wrong values or request were passed to the kernel
 *
 * The first word of the message, the source endpoint, is not copied: the
 * kernel fills it in.
 *
 * A pointer that does not lie entirely in the user half of the address
 * space is refused outright; it must not get us to read kernel memory.
 */
ENTRY(copy_msg_from_user)
	movabsq	$(X86_64_VM_USER_TOP - MESSAGE_SIZE), %rax
	cmpq	%rax, %rdi
	ja	_C_LABEL(__user_copy_msg_pointer_failure)
	cld
	xchgq	%rdi, %rsi	/* %rsi = user buffer, %rdi = kernel buffer */
	addq	$4, %rsi	/* skip m_source */
	addq	$4, %rdi
	movl	$(MESSAGE_SIZE - 4), %ecx
	rep	movsb
LABEL(__copy_msg_from_user_end)
	xorl	%eax, %eax
	ret

/*===========================================================================*/
/*				copy_msg_to_user			     */
/*===========================================================================*/
/*
 * void copy_msg_to_user(message * src, message * user_mbuf);
 *
 * Copies a message to user process space from a kernel buffer.
 *
 * All the other copy_msg_from_user() comments apply here as well!
 */
ENTRY(copy_msg_to_user)
	movabsq	$(X86_64_VM_USER_TOP - MESSAGE_SIZE), %rax
	cmpq	%rax, %rsi
	ja	_C_LABEL(__user_copy_msg_pointer_failure)
	cld
	xchgq	%rdi, %rsi	/* %rsi = kernel buffer, %rdi = user buffer */
	movl	$(MESSAGE_SIZE / 8), %ecx
	rep	movsq
LABEL(__copy_msg_to_user_end)
	xorl	%eax, %eax
	ret

/*
 * if a function from a selected set of copies from or to userspace fails, it is
 * because of a wrong pointer supplied by the userspace. We have to clean up and
 * and return -1 to indicated that something wrong has happend. The place it was
 * called from has to handle this situation. The exception handler redirect us
 * here to continue, clean up and report the error
 */
ENTRY(__user_copy_msg_pointer_failure)
	movl	$-1, %eax
	ret

/*===========================================================================*/
/*				phys_memset				     */
/*===========================================================================*/
/*
 * PUBLIC vir_bytes phys_memset(phys_bytes dst, unsigned long pattern,
 *	phys_bytes bytecount);
 * Fill a block of memory with a 4-byte pattern.  As with phys_copy, the
 * address is a virtual one.
 */
ENTRY(phys_memset)
	cld
	movl	%esi, %eax	/* pattern */
	movq	%rdx, %rcx
	shrq	$2, %rcx
	rep stosl

/* Any remaining bytes? */
	movq	%rdx, %rcx
	andq	$3, %rcx
	rep stosb

LABEL(memset_fault)		/* kernel can send us here */
	xorl	%eax, %eax		/* 0 means: no fault */
	ret

LABEL(memset_fault_in_kernel)		/* kernel can send us here */
	movq	%cr2, %rax
	ret

/*===========================================================================*/
/*				x86_triplefault				     */
/*===========================================================================*/
/*
 * PUBLIC void x86_triplefault();
 * Reset the system by loading IDT with offset 0 and interrupting.
 */
ENTRY(x86_triplefault)
	lidt	idt_zero(%rip)
	int	$3	/* anything goes, the CPU will not like it */

.data
idt_zero:
.quad	0, 0
.text


/*===========================================================================*/
/*			      	halt_cpu				     */
/*===========================================================================*/
/*
 * PUBLIC void halt_cpu(void);
 * reanables interrupts and puts the cpu in the halts state. Once an interrupt
 * is handled the execution resumes by disabling interrupts and continues
 */
ENTRY(halt_cpu)
	sti
	hlt /* interrupts enabled only after this instruction is executed! */
	/*
	 * interrupt handlers make sure that the interrupts are disabled when we
	 * get here so we take only _one_ interrupt after halting the CPU.
	 *
	 * HLT may however also resume without any interrupt handler having
	 * run (seen under KVM; NMI, SMI and spurious wakeups do the same).
	 * Interrupts would then stay enabled in the kernel, and the next
	 * halt_cpu() call's STI would no longer delay interrupts by one
	 * instruction: an interrupt taken right before the HLT returns to it
	 * with interrupts disabled, halting the CPU for good (system freeze).
	 * So disable interrupts again explicitly.
	 */
	cli
	ret

/*===========================================================================*/
/*			poweroff_vmware_clihlt				     */
/*===========================================================================*/
/*
 * PUBLIC void poweroff_vmware_clihlt(void);
 * VMware detects this peculiar sequence and forces the virtual machine off
 * when the parameter gui.exitOnCLIHLT is set to TRUE.
 * Otherwise this sequence just hangs the CPU, requiring a power down action.
 */
ENTRY(poweroff_vmware_clihlt)
	push	%rbx
#ifndef NO_VMWARE_DETECTION
	movl	$1, %eax
	cpuid
	testl	$(1<<31), %ecx /* "virtualized" */
	jz	1f	/* always 0 on real hardware */
	movl	$0x40000000, %eax /* select hypervisor-use leaf */
	cpuid
	cmpl	$0x61774D56, %ebx /* ASCII "VMwa" */
	jne	1f
	cmpl	$0x4D566572, %ecx /* ASCII "reVM" */
	jne	1f
	cmpl	$0x65726177, %edx /* ASCII "ware" */
	jne	1f
	/* we are virtualized by some VMware product! */
#endif
	cli
	hlt
1:	pop	%rbx
	ret

/*===========================================================================*/
/*			      read_flags				     */
/*===========================================================================*/
/*
 * PUBLIC unsigned long read_cpu_flags(void);
 * Read CPU status flags from C.
 */
ENTRY(read_cpu_flags)
	pushf
	pop	%rax
	ret

ENTRY(read_ds)
	xorl	%eax, %eax
	movw	%ds, %ax
	ret

ENTRY(read_cs)
	xorl	%eax, %eax
	movw	%cs, %ax
	ret

ENTRY(read_ss)
	xorl	%eax, %eax
	movw	%ss, %ax
	ret

/*===========================================================================*/
/*                            fpu_routines                                   */
/*===========================================================================*/

/* non-waiting FPU initialization */
ENTRY(fninit)
	fninit
	ret

ENTRY(clts)
	clts
	ret

/* store status word (non-waiting) */
ENTRY(fnstsw)
	xorl     %eax, %eax
	fnstsw	%ax
	ret

/* void fnstcw(unsigned short *cw) */
ENTRY(fnstcw)
	fnstcw	(%rdi)
	ret

/* void fxsave(void *state): save the FPU/SSE state in 64-bit format */
ENTRY(fxsave)
	rex64 fxsave (%rdi)
	ret

/* void ldmxcsr(u32_t *value) */
ENTRY(ldmxcsr)
	ldmxcsr	(%rdi)
	ret

/*===========================================================================*/
/*				fxrstor					     */
/*===========================================================================*/
/* int fxrstor(void *state): restore; returns nonzero if the state was bad
 * (the exception handler then sends us to __frstor_failure) */
ENTRY(fxrstor)
	rex64 fxrstor (%rdi)
ENTRY(__fxrstor_end)
	xorl     %eax, %eax
	ret

/* Exception handler target for a failing fxrstor. */
ENTRY(__frstor_failure)
	movl	$1, %eax
	ret

/* Read/write control registers */
ENTRY(read_cr0)
	movq	%cr0, %rax
	ret
ENTRY(read_cr2)
	movq	%cr2, %rax
	ret
ENTRY(read_cr3)
	movq	%cr3, %rax
	ret
ENTRY(read_cr4)
	movq	%cr4, %rax
	ret
ENTRY(write_cr0)
	movq	%rdi, %cr0
	jmp	0f /* a jump is required for some sets */
0:	ret
ENTRY(write_cr3)
	movq	%rdi, %cr3
	ret
ENTRY(write_cr4)
	movq	%rdi, %cr4
	jmp	0f
0:	ret

/* Read/write various descriptor tables */
ENTRY(x86_ltr)
	ltr	%di
	ret
ENTRY(x86_lidt)
	lidt	(%rdi)
	ret
ENTRY(x86_lgdt)
	lgdt	(%rdi)
	ret
ENTRY(x86_lldt)
	lldt	%di
	ret
ENTRY(x86_sgdt)
	sgdt	(%rdi)
	ret
ENTRY(x86_sidt)
	sidt	(%rdi)
	ret

/* Load segments */
ENTRY(x86_load_ds)
	movw	%di, %ds
	ret
ENTRY(x86_load_es)
	movw	%di, %es
	ret
ENTRY(x86_load_fs)
	movw	%di, %fs
	ret
ENTRY(x86_load_gs)
	movw	%di, %gs
	ret
ENTRY(x86_load_ss)
	movw	%di, %ss
	ret

/* invlpg */
ENTRY(i386_invlpg)
	invlpg	(%rdi)
	ret

/* void x86_load_kerncs(void): reload %cs with the kernel code segment; in
 * long mode that takes a far return */
ENTRY(x86_load_kerncs)
	pop	%rax			/* return address */
	push	$KERN_CS_SELECTOR
	push	%rax
	lretq

/*
 * Read the Model Specific Register (MSR) of IA32 architecture
 *
 * void ia32_msr_read(u32_t reg, u32_t * hi, u32_t * lo)
 */
ENTRY(ia32_msr_read)
	movq	%rdx, %r8	/* lo pointer; rdmsr clobbers %rdx */
	movl	%edi, %ecx
	rdmsr
	movl	%edx, (%rsi)
	movl	%eax, (%r8)
	ret

/*
 * Write the Model Specific Register (MSR) of IA32 architecture
 *
 * void ia32_msr_write(u32_t reg, u32_t hi, u32_t lo)
 */
ENTRY(ia32_msr_write)
	movl	%edi, %ecx
	movl	%edx, %eax	/* lo */
	movl	%esi, %edx	/* hi */
	wrmsr
	ret

/*===========================================================================*/
/*			      __switch_address_space			     */
/*===========================================================================*/
/* PUBLIC void __switch_address_space(struct proc *p, struct ** ptproc)
 *
 * sets the %cr3 register to the supplied value if it is not already set to the
 * same value in which case it would only result in an extra TLB flush which is
 * not desirable
 */
ENTRY(__switch_address_space)
	/* get the new cr3 value */
	movq	P_CR3(%rdi), %rax
	/* test if the new cr3 != NULL */
	testq	%rax, %rax
	jz	0f

	/*
	 * test if the cr3 is loaded with the current value to avoid unnecessary
	 * TLB flushes
	 */
	movq	%cr3, %rcx
	cmpq	%rcx, %rax
	je	0f
	movq	%rax, %cr3
	/* set ptproc */
	movq	%rdi, (%rsi)
0:
	ret

/* acknowledge just the master PIC */
ENTRY(eoi_8259_master)
	movb	$END_OF_INT, %al
	outb	$INT_CTL
	ret

/* we have to acknowledge both PICs */
ENTRY(eoi_8259_slave)
	movb	$END_OF_INT, %al
	outb	$INT_CTL
	outb	$INT2_CTL
	ret

/* in some cases we need to force TLB update, reloading cr3 does the trick */
ENTRY(refresh_tlb)
	movq	%cr3, %rax
	movq	%rax, %cr3
	ret

/*===========================================================================*/
/*			      mfence					     */
/*===========================================================================*/
/*  PUBLIC void mfence (void); */
/*  architecture specific memory barrier routine. */
ENTRY(mfence)
	mfence
	ret

/*===========================================================================*/
/*			      arch_pause				     */
/*===========================================================================*/
/*  PUBLIC void arch_pause (void); */
/*  architecture specific pause routine. */
ENTRY(arch_pause)
	pause
	ret

/*===========================================================================*/
/*			      read_ebp				     	     */
/*===========================================================================*/
ENTRY(read_ebp)
	movq	%rbp, %rax
	ret

ENTRY(interrupts_enable)
	sti
	ret

ENTRY(interrupts_disable)
	cli
	ret


/*
 * void switch_k_stack(void * rsp, void (* continuation)(void));
 *
 * sets the current stack pointer to the given value and continues execution at
 * the given address
 */
ENTRY(switch_k_stack)
	xorl	%ebp, %ebp	/* reset %rbp for stack trace */
	movq	%rdi, %rsp	/* set the new stack */
	jmp	*%rsi		/* and jump to the continuation */

	/* NOT_REACHABLE */
0:	jmp	0b

/*===========================================================================*/
/*			port I/O and interrupt flag			     */
/*===========================================================================*/
/* unsigned inb(u16_t port), inw, inl; void outb(u16_t port, u8_t value), ... */
ENTRY(inb)
	movl	%edi, %edx
	xorl	%eax, %eax
	inb	%dx
	ret
ENTRY(inw)
	movl	%edi, %edx
	xorl	%eax, %eax
	inw	%dx
	ret
ENTRY(inl)
	movl	%edi, %edx
	inl	%dx
	ret
ENTRY(outb)
	movl	%edi, %edx
	movl	%esi, %eax
	outb	%dx
	ret
ENTRY(outw)
	movl	%edi, %edx
	movl	%esi, %eax
	outw	%dx
	ret
ENTRY(outl)
	movl	%edi, %edx
	movl	%esi, %eax
	outl	%dx
	ret
ENTRY(intr_disable)
	cli
	ret
ENTRY(intr_enable)
	sti
	ret

/*===========================================================================*/
/*				debug registers				     */
/*===========================================================================*/
#define LD_ST_REG(reg)						;\
ENTRY(ld_##reg)							;\
	movq	%rdi, %reg					;\
	ret							;\
								;\
ENTRY(st_##reg)							;\
	movq	%reg, %rax					;\
	ret

LD_ST_REG(dr0)
LD_ST_REG(dr1)
LD_ST_REG(dr2)
LD_ST_REG(dr3)
LD_ST_REG(dr6)
LD_ST_REG(dr7)
