#ifndef _IPC_CONST_H
#define _IPC_CONST_H

#include <machine/ipcconst.h>

 /* System call numbers that are passed when trapping to the kernel. */
#define SEND		   1	/* blocking send */
#define RECEIVE		   2	/* blocking receive */
#define SENDREC	 	   3  	/* SEND + RECEIVE */
#define NOTIFY		   4	/* asynchronous notify */
#define SENDNB             5    /* nonblocking send */
#define MINIX_KERNINFO     6    /* request kernel info structure */
#define SENDA		   16	/* asynchronous send */
#define IPCNO_HIGHEST	SENDA
/* Size of a message: a header (source endpoint and type, 8 bytes) followed
 * by the payload.  On ILP32 architectures the payload is 56 bytes and every
 * message type is laid out, by hand, to be exactly that big.  On LP64
 * architectures the pointers, sizes and longs in those same types are twice
 * as wide, so a type can grow up to 112 bytes; there the payload is 120
 * bytes (a 128-byte message) and the types only have to fit in it.
 */
#ifdef _LP64
#define _MSG_PAYLOAD_SIZE	120
#else
#define _MSG_PAYLOAD_SIZE	56
#endif
#define _MSG_HEADER_SIZE	8
#define _MSG_SIZE		(_MSG_HEADER_SIZE + _MSG_PAYLOAD_SIZE)

/* Check that the message payload type doesn't grow past the maximum IPC payload size.
 * This is a compile time check. */
#ifdef _LP64
#define _ASSERT_MSG_SIZE(msg_type) \
    typedef int _ASSERT_##msg_type[/* CONSTCOND */ \
	sizeof(msg_type) <= _MSG_PAYLOAD_SIZE ? 1 : -1]
#else
#define _ASSERT_MSG_SIZE(msg_type) \
    typedef int _ASSERT_##msg_type[/* CONSTCOND */sizeof(msg_type) == 56 ? 1 : -1]
#endif

/* Macros for IPC status code manipulation. */
#define IPC_STATUS_CALL_SHIFT	0
#define IPC_STATUS_CALL_MASK	0x3F
#define IPC_STATUS_CALL(status)	\
	(((status) >> IPC_STATUS_CALL_SHIFT) & IPC_STATUS_CALL_MASK)
#define IPC_STATUS_CALL_TO(call) \
	(((call) & IPC_STATUS_CALL_MASK) << IPC_STATUS_CALL_SHIFT)

#define IPC_FLG_MSG_FROM_KERNEL	1 /* this message originated in the kernel on
				     behalf of a process, this is a trusted
				     message, never reply to the sender
				 */
#define IPC_STATUS_FLAGS_SHIFT	16
#define IPC_STATUS_FLAGS(flgs)	((flgs) << IPC_STATUS_FLAGS_SHIFT)
#define IPC_STATUS_FLAGS_TEST(status, flgs)	\
		(((status) >> IPC_STATUS_FLAGS_SHIFT) & (flgs))
#endif /* IPC_CONST_H */
