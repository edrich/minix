
#ifndef _PT_H
#define _PT_H 1

#include <machine/vm.h>

#include "vm.h"
#include "pagetable.h"

#if defined(__x86_64__)

/* A pagetable: a 4-level tree. VM reaches every page of it through its
 * direct map of physical memory, so all there is to remember is the root.
 */
typedef struct {
	/* Top level table (PML4) in VM addr space - root of page table.  */
	u64_t *pt_dir;		/* page aligned */
	phys_bytes pt_dir_phys;	/* physical address of pt_dir */

	/* When looking for a hole in virtual address space, start
	 * looking here. This is just a hint.
	 */
	vir_bytes pt_virtop;
} pt_t;

#else /* !defined(__x86_64__) */

/* A pagetable. */
typedef struct {
	/* Directory entries in VM addr space - root of page table.  */
	u32_t *pt_dir;		/* page aligned (ARCH_VM_DIR_ENTRIES) */
	u32_t pt_dir_phys;	/* physical address of pt_dir */

	/* Pointers to page tables in VM address space. */
	u32_t *pt_pt[ARCH_VM_DIR_ENTRIES];

	/* When looking for a hole in virtual address space, start
	 * looking here. This is in linear addresses, i.e.,
	 * not as the process sees it but the position in the page
	 * page table. This is just a hint.
	 */
	u32_t pt_virtop;
} pt_t;

#endif /* !defined(__x86_64__) */

#define CLICKSPERPAGE (VM_PAGE_SIZE/CLICK_SIZE)

#endif
