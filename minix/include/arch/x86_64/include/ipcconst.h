#ifndef _X86_64_IPCCONST_H_
#define _X86_64_IPCCONST_H_

#define KERVEC_INTR 32     /* syscall trap to kernel */
#define IPCVEC_INTR 33     /* ipc trap to kernel  */

#define KERVEC_UM 34     /* syscall trap to kernel, user-mapped code */
#define IPCVEC_UM 35     /* ipc trap to kernel, user-mapped code  */

/* Register conventions of the IPC trap (they follow the C calling
 * convention, so the stubs need no shuffling):
 *   rdi  source/destination endpoint (SENDA: table size)
 *   rsi  message pointer (SENDA: table pointer)
 *   rdx  IPC call number (SEND, RECEIVE, ...)
 * On return rax holds the result and rdx the IPC status (or, for
 * MINIX_KERNINFO, the address of the kernel info structure).
 * The kernel call trap takes the message pointer in rdi.
 */
#define IPC_STATUS_REG		dx

#endif  /* _X86_64_IPCCONST_H_ */
