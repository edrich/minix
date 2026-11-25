/*	minix/u64.h					Author: Kees J. Bot
 *								7 Dec 1995
 * Functions to manipulate 64 bit disk addresses.
 */
#ifndef _MINIX__U64_H
#define _MINIX__U64_H

#include <sys/types.h>

/* The halves are 32 bits wide on every architecture; the return type stays
 * unsigned long, which existing callers print with %lx.
 */
static inline unsigned long ex64lo(u64_t i)
{
	return (unsigned long)(u32_t)i;
}

static inline unsigned long ex64hi(u64_t i)
{
	return (unsigned long)(u32_t)(i>>32);
}

static inline u64_t make64(unsigned long lo, unsigned long hi)
{
	return ((u64_t)(u32_t)hi << 32) | (u64_t)(u32_t)lo;
}

#endif /* _MINIX__U64_H */
