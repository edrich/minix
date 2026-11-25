#include <machine/asm.h>

#if defined(__x86_64__)

IMPORT(remaining_invocations)
IMPORT(origstate)
IMPORT(newstate)

#define JUNK 0xCC0FFEE000000000

/* Store all general purpose registers but %rsp, and %rsp last, in dest[].
 * RIP-relative stores change no registers. The order is that of REGNAMES
 * in test57.c.
 */
#define SAVE(dest)					\
	movq	%rax,  0*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rbx,  1*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rcx,  2*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rdx,  3*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rsi,  4*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rdi,  5*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rbp,  6*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r8,   7*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r9,   8*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r10,  9*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r11, 10*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r12, 11*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r13, 12*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r14, 13*8+_C_LABEL(dest)(%rip)	;	\
	movq	%r15, 14*8+_C_LABEL(dest)(%rip)	;	\
	movq	%rsp, 15*8+_C_LABEL(dest)(%rip)

/* void check_context_loop() */
ENTRY(check_context_loop)
	/* Save the callee-saved registers so we can restore them. */
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	/* Put some junk in the registers, differently per register, so
	 * that corruption is likelier to be detected. %rsp stays as it is
	 * and is checked as well.
	 */
	movabsq	$JUNK+1, %rax
	movabsq	$JUNK+2, %rbx
	movabsq	$JUNK+3, %rcx
	movabsq	$JUNK+4, %rdx
	movabsq	$JUNK+5, %rsi
	movabsq	$JUNK+6, %rdi
	movabsq	$JUNK+7, %rbp
	movabsq	$JUNK+8, %r8
	movabsq	$JUNK+9, %r9
	movabsq	$JUNK+10, %r10
	movabsq	$JUNK+11, %r11
	movabsq	$JUNK+12, %r12
	movabsq	$JUNK+13, %r13
	movabsq	$JUNK+14, %r14
	movabsq	$JUNK+15, %r15

	SAVE(origstate)
cont:
	/* Spin until the signal handler has run often enough. The flags
	 * are not compared, so the comparison may change them. */
	cmpl	$0, _C_LABEL(remaining_invocations)(%rip)
	jnz	cont

	SAVE(newstate)

	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	ret

#else /* !defined(__x86_64__) */

IMPORT(remaining_invocations)
IMPORT(origstate)
IMPORT(newstate)

#define JUNK 0xCC0FFEE0

#define COPY(dest, offset)	\
	mov	$dest, %ebp		;	\
	mov	4*offset(%esp), %ebx	;	\
	mov	%ebx, 4*offset(%ebp) ;

/* Copy the result of a pusha to dest. */
#define COPYA(dest) \
	COPY(dest, 0); COPY(dest, 1); COPY(dest, 2); COPY(dest, 3); \
	COPY(dest, 4); COPY(dest, 5); COPY(dest, 6); COPY(dest, 7); 

/* void check_context_loop() */
ENTRY(check_context_loop)
	/* Save original context so we can restore it. */
	pusha

	/* Put some junk in the registers.
	 * We want to junk the state, and junk it differently per reg,
	 * so it's likelier corruption is actually detected. We can't
	 * touch %esp but we can verify that it doesn't change from its
	 * current value.
	 */
	mov	$JUNK+1, %eax
	mov	$JUNK+2, %ebx
	mov	$JUNK+3, %ecx
	mov	$JUNK+4, %edx
	mov	$JUNK+5, %ebp
	mov	$JUNK+6, %esi
	mov	$JUNK+7, %edi

	/* Save the junked state so we can compare it. */
	pusha
cont:
	/* Check if we're done. */
	cmpl	$0, (_C_LABEL(remaining_invocations))
	jz	done

	/* We're not done. */

	/* Restart loop. */
	jmp	cont

done:
	/* Save the junked, but should be unmodified state
	 * so we can copy it.
	 */
	pusha
	COPYA(_C_LABEL(newstate));
	popa

	/* copy and restore junked state */
	COPYA(_C_LABEL(origstate));
	popa

	/* restore original state and return */
	popa
	ret

#endif /* !defined(__x86_64__) */
