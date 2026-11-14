#include <minix/ipcconst.h>
#include <machine/asm.h>

/**========================================================================* */
/*                           IPC assembly routines			  * */
/**========================================================================* */
/*
 * The IPC trap takes its arguments in the registers the C calling
 * convention already put them in: rdi = source/destination endpoint,
 * rsi = message pointer.  Only the call number has to be added, in rdx.
 * The kernel returns the result in rax and the IPC status in rdx; all
 * other registers are preserved.
 */

/* int _ipc_send_intr(endpoint_t dest, message *m_ptr) */
ENTRY(_ipc_send_intr)
	movl	$SEND, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	ret

/* int _ipc_receive_intr(endpoint_t src, message *m_ptr, int *status_ptr) */
ENTRY(_ipc_receive_intr)
	push	%rdx		/* save the status pointer */
	movl	$RECEIVE, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	pop	%rcx		/* rcx = status pointer */
	movl	%edx, (%rcx)
	ret

/* int _ipc_sendrec_intr(endpoint_t src_dest, message *m_ptr) */
ENTRY(_ipc_sendrec_intr)
	movl	$SENDREC, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	ret

/* int _ipc_notify_intr(endpoint_t dest) */
ENTRY(_ipc_notify_intr)
	movl	$NOTIFY, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	ret

/* int _ipc_sendnb_intr(endpoint_t dest, message *m_ptr) */
ENTRY(_ipc_sendnb_intr)
	movl	$SENDNB, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	ret

/* int _ipc_senda_intr(asynmsg_t *table, size_t count) */
ENTRY(_ipc_senda_intr)
	xchgq	%rdi, %rsi	/* rdi = count, rsi = table */
	movl	$SENDA, %edx
	int	$IPCVEC_INTR	/* trap to the kernel */
	ret
