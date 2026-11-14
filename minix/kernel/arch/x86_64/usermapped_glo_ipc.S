#include <minix/ipcconst.h>
#include <machine/asm.h>

/* Redefine _ENTRY to put the following function in the section
 * ".usermapped_glo". */
#undef _ENTRY
#define _ENTRY(x) \
	_ALIGN_TEXT; .globl x; .type x,@function; x:

.section .usermapped_glo, "wax", @progbits

/**========================================================================* */
/*                           IPC assembly routines			  * */
/**========================================================================* */
/*
 * The kernel maps these into every process; libc calls them through the
 * minix_ipcvecs table instead of its own built-in stubs.  Only the software
 * interrupt flavour exists so far.  See <machine/ipcconst.h> for the
 * register conventions of the traps.
 */

/* int usermapped_send_softint(endpoint_t dest, message *m_ptr) */
ENTRY(usermapped_send_softint)
	movl	$SEND, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	ret

/* int usermapped_receive_softint(endpoint_t src, message *m_ptr,
 *	int *status_ptr) */
ENTRY(usermapped_receive_softint)
	push	%rdx		/* save the status pointer */
	movl	$RECEIVE, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	pop	%rcx		/* rcx = status pointer */
	movl	%edx, (%rcx)
	ret

/* int usermapped_sendrec_softint(endpoint_t src_dest, message *m_ptr) */
ENTRY(usermapped_sendrec_softint)
	movl	$SENDREC, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	ret

/* int usermapped_sendnb_softint(endpoint_t dest, message *m_ptr) */
ENTRY(usermapped_sendnb_softint)
	movl	$SENDNB, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	ret

/* int usermapped_notify_softint(endpoint_t dest) */
ENTRY(usermapped_notify_softint)
	movl	$NOTIFY, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	ret

/* int usermapped_senda_softint(asynmsg_t *table, size_t count) */
ENTRY(usermapped_senda_softint)
	xchgq	%rdi, %rsi	/* rdi = count, rsi = table */
	movl	$SENDA, %edx
	int	$IPCVEC_UM	/* trap to the kernel */
	ret

/* int usermapped_do_kernel_call_softint(message *m_ptr) */
ENTRY(usermapped_do_kernel_call_softint)
	int	$KERVEC_UM	/* message pointer is in %rdi */
	ret
