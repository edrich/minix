/* get_bp.s */
/* */
/* return RBP in RAX */

#include <machine/asm.h>

ENTRY(get_bp)
	movq	%rbp, %rax
	ret
