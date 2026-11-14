#ifndef STACK_FRAME_H
#define STACK_FRAME_H

#include <sys/types.h>

typedef unsigned long reg_t;         /* machine register */
typedef reg_t segdesc_t;

/* The stack frame layout is determined by the software, but for efficiency
 * it is laid out so the assembly code to use it is as simple as possible.
 * The last five members are the frame the CPU pushes on an interrupt in
 * long mode (it always pushes ss:rsp there); everything above them is
 * saved by the kernel entry code.  The names of the legacy registers are
 * the same as on i386, so that machine independent code can be shared.
 */
struct stackframe_s {
	reg_t r15;                    /* last item saved by the entry code */
	reg_t r14;
	reg_t r13;
	reg_t r12;
	reg_t r11;
	reg_t r10;
	reg_t r9;
	reg_t r8;
	reg_t di;
	reg_t si;
	reg_t fp;                     /* rbp */
	reg_t bx;
	reg_t dx;
	reg_t cx;
	reg_t retreg;                 /* rax */
	reg_t pc;                     /*  ^  last item pushed by interrupt */
	reg_t cs;                     /*  | */
	reg_t psw;                    /*  | */
	reg_t sp;                     /*  | */
	reg_t ss;                     /* these are pushed by CPU during interrupt */
};

#endif /* #ifndef STACK_FRAME_H */
