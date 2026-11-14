#include <minix/ipcconst.h>
#include <machine/asm.h>

/* int _do_kernel_call_intr(message *m_ptr) */
ENTRY(_do_kernel_call_intr)
	/* the message pointer is passed to the kernel in %rdi, where the
	 * C calling convention already put it */
	int	$KERVEC_INTR
	ret
