#include "kernel/kernel.h" /* configures the kernel */

/* sections */

#include <machine/vm.h>
#include "kernel/kernel.h"
#include <minix/config.h>
#include <minix/const.h>
#include <minix/com.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include "archconst.h"
#include "sconst.h"
#include <machine/multiboot.h>

#include "arch_proto.h" /* K_STACK_SIZE */

/*
 * Startup code.  The kernel is loaded by a Multiboot (v1) boot loader, which
 * leaves the CPU in 32-bit protected mode with paging off.  The code here is
 * linked at the address the kernel is loaded at; it builds a temporary
 * identity mapping of the first 4GB, switches the CPU to long mode and then
 * calls pre_init(), which makes the real page table.
 *
 * The kernel is a 64-bit ELF object that is converted to a 32-bit one after
 * linking, because Multiboot loaders only take those; see Makefile.inc.
 */

#define BOOT_CS		0x08
#define BOOT_DS		0x10

.text
.code32
/*===========================================================================*/
/*				MINIX				     */
/*===========================================================================*/
.global MINIX
MINIX:
/* this is the entry point for the MINIX kernel */
	jmp multiboot_init

/* Multiboot header here*/

.balign 8

#define MULTIBOOT_FLAGS (MULTIBOOT_HEADER_WANT_MEMORY | MULTIBOOT_HEADER_MODS_ALIGNED)

multiboot_magic:
	.long MULTIBOOT_HEADER_MAGIC
multiboot_flags:
	.long MULTIBOOT_FLAGS
multiboot_checksum:
	.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
/* Video mode */
multiboot_mode_type:
	.long MULTIBOOT_VIDEO_MODE_EGA
multiboot_width:
	.long MULTIBOOT_CONSOLE_COLS
multiboot_height:
	.long MULTIBOOT_CONSOLE_LINES
multiboot_depth:
	.long 0

multiboot_init:
	cli
	cld
	movl	$load_stack_start, %esp	/* make usable stack */

	/* keep the multiboot magic number and information struct pointer
	 * where pre_init() wants them, as its first two arguments */
	movl	%eax, %edi
	movl	%ebx, %esi

	/* Identity map the first 4GB with 2MB pages: four page
	 * directories of 512 entries each, which are contiguous. */
	movl	$boot_pd, %ebx
	xorl	%ecx, %ecx
1:	movl	%ecx, %eax
	shll	$21, %eax		/* low 32 bits of ecx * 2MB */
	orl	$(I386_VM_PRESENT | I386_VM_WRITE | I386_VM_BIGPAGE), %eax
	movl	%eax, (%ebx,%ecx,8)
	movl	%ecx, %edx
	shrl	$11, %edx		/* high 32 bits of ecx * 2MB */
	movl	%edx, 4(%ebx,%ecx,8)
	incl	%ecx
	cmpl	$(4 * 512), %ecx
	jb	1b

	/* the first four page directory pointers point to those */
	movl	$boot_pdpt, %ebx
	movl	$boot_pd, %eax
	orl	$(I386_VM_PRESENT | I386_VM_WRITE), %eax
	xorl	%ecx, %ecx
2:	movl	%eax, (%ebx,%ecx,8)
	movl	$0, 4(%ebx,%ecx,8)
	addl	$I386_PAGE_SIZE, %eax
	incl	%ecx
	cmpl	$4, %ecx
	jb	2b

	/* and the first entry of the top level table to that */
	movl	$boot_pdpt, %eax
	orl	$(I386_VM_PRESENT | I386_VM_WRITE), %eax
	movl	%eax, boot_pml4
	movl	$0, boot_pml4 + 4

	/* enable PAE, load the page table, enable long mode and then
	 * paging, which activates long mode */
	movl	%cr4, %eax
	orl	$I386_CR4_PAE, %eax
	movl	%eax, %cr4

	movl	$boot_pml4, %eax
	movl	%eax, %cr3

	movl	$AMD_MSR_EFER, %ecx
	rdmsr
	orl	$AMD_EFER_LME, %eax
	wrmsr

	movl	%cr0, %eax
	orl	$I386_CR0_PG, %eax
	movl	%eax, %cr0

	/* we are in compatibility mode now; a far jump to a 64-bit code
	 * segment gets us to 64-bit mode */
	lgdt	boot_gdt_desc
	ljmp	$BOOT_CS, $long_mode

.code64
long_mode:
	movw	$BOOT_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	xorl	%eax, %eax
	movw	%ax, %fs
	movw	%ax, %gs

	movl	$load_stack_start, %esp	/* zero extends into %rsp */
	xorl	%ebp, %ebp
	push	$0	/* set flags to known good state */
	popf	/* clear nested task and int enable */

	movl	%edi, %edi	/* multiboot magic number, zero extended */
	movl	%esi, %esi	/* multiboot information struct */
        call    _C_LABEL(pre_init)

	/* Kernel is mapped high now and ready to go, with
	 * the boot info pointer returnd in %rax. Set the
	 * highly mapped stack, pass the boot info pointer
	 * and jump to the highly mapped kernel.
	 */
	movabsq	$k_initial_stktop, %rsp
	xorl	%ebp, %ebp	/* Terminate stack */
	movq	%rax, %rdi
	movabsq	$_C_LABEL(kmain), %rax
	call	*%rax

	/* not reached */
hang:
	jmp hang

.data
.balign 16
boot_gdt:
	.quad	0			/* null descriptor */
	.quad	0x00209A0000000000	/* 64-bit code: present, executable, L */
	.quad	0x0000920000000000	/* data: present, writable */
boot_gdt_desc:
	.short	boot_gdt_desc - boot_gdt - 1
	.long	boot_gdt

.balign 4096
boot_pml4:
	.space 4096
boot_pdpt:
	.space 4096
boot_pd:
	.space 4 * 4096
load_stack:
	.space 4096
load_stack_start:
