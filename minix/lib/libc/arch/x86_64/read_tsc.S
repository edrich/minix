#include <machine/asm.h>

/**===========================================================================* */
/* PUBLIC void read_tsc(u32_t *high, u32_t *low); */
/* Read the cycle counter of the CPU. */
ENTRY(read_tsc)
	rdtsc			/* places the TSC in EDX:EAX */
	movl	%edx, (%rdi)
	movl	%eax, (%rsi)
	ret

/**===========================================================================* */
/* PUBLIC void read_host_time_ns(u32_t *high, u32_t *low); */
/* access real time in ns from host in vmware. */
ENTRY(read_host_time_ns)
	movl	$0x10001, %ecx
	rdtsc
	movl	%edx, (%rdi)
	movl	%eax, (%rsi)
	ret
