/* _cpuid() - interface to cpuid instruction */

/* void _cpuid(u32_t *eax, u32_t *ebx, u32_t *ecx, u32_t *edx); */

#include <machine/asm.h>

ENTRY(_cpuid)
	push	%rbx
	movq	%rdx, %r8		/* r8 = &ecx */
	movq	%rcx, %r9		/* r9 = &edx */
/* load the four input values and execute cpuid */
	movl	(%rdi), %eax
	movl	(%rsi), %ebx
	movl	(%r8), %ecx
	movl	(%r9), %edx
	cpuid
/* store results in pointer arguments */
	movl	%eax, (%rdi)
	movl	%ebx, (%rsi)
	movl	%ecx, (%r8)
	movl	%edx, (%r9)
	pop	%rbx
	ret
