#include "kernel/kernel.h"
#include "arch_proto.h"

struct minix_ipcvecs minix_ipcvecs_softint __section(".usermapped") = {
	.send		= usermapped_send_softint,
	.receive	= usermapped_receive_softint,
	.sendrec	= usermapped_sendrec_softint,
	.sendnb		= usermapped_sendnb_softint,
	.notify		= usermapped_notify_softint,
	.do_kernel_call	= usermapped_do_kernel_call_softint,
	.senda		= usermapped_senda_softint
};
