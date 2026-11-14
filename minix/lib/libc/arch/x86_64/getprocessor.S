/*	getprocessor() - determine processor type */

#include <machine/asm.h>

/* int getprocessor(void); */
/*	Return 586, 686, ...  Every long mode capable CPU has cpuid. */

ENTRY(getprocessor)
	push	%rbx
	movl	$1, %eax
	cpuid
	shrl	$8, %eax
	andl	$0x0F, %eax	/* Extract the family (5, 6, ...) */
	cmpl	$15, %eax	/* 15: extended family */
	jne	1f
	movl	$6, %eax	/* Make it 686 */
1:	imull	$100, %eax	/* 500, 600, ... */
	addl	$86, %eax	/* 586, 686, ... */
	pop	%rbx
	ret
