
/* Page tables for x86_64: 4 levels (PML4, PDPT, PD, PT) of 512 entries.
 *
 * This implements the same interface as ../../pagetable.c does for the
 * architectures with 2-level page tables, but works differently inside.
 * VM has, in its own address space, a "direct map" of all of physical
 * memory that the kernel set up for it (X86_64_VM_DMAP_BASE, see
 * <machine/vm.h>).  Every page table page, and every page VM allocates for
 * its own use, is simply accessed through that.  There is therefore no
 * bookkeeping of where page tables are mapped, no hunting for holes in
 * VM's address space, and none of the recursion (and pool of spare pages
 * to break it) that mapping a page table for the sake of mapping a page
 * brings.
 *
 * The kernel half of every address space (PML4 entries 256-511) is copied
 * from the bootstrap page table the kernel made; the lower level tables
 * behind those entries are shared by all processes.  The same goes for
 * the one PML4 entry (X86_64_VM_KERNMAP_PML4E) that holds the mappings the
 * kernel asks VM to make on its behalf.
 */

#define _SYSTEM 1

#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ds.h>
#include <minix/endpoint.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/safecopies.h>
#include <minix/cpufeature.h>
#include <minix/bitmap.h>
#include <minix/debug.h>

#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#include "proto.h"
#include "glo.h"
#include "util.h"
#include "vm.h"
#include "sanitycheck.h"

#if CLICK_SIZE != VM_PAGE_SIZE
#error CLICK_SIZE must be page size.
#endif

static int vm_self_pages;

/* Our process table entry. */
struct vmproc *vmprocess = &vmproc[VM_PROC_NR];

/* Kept for the benefit of the generic code; there are no spare page
 * directories here.
 */
int missing_sparedirs = 0;

#define NR_ENTRIES	512		/* in a table of any level */
#define KERN_PML4E	256		/* first PML4 entry of the kernel */

/* The kernel half of the bootstrap page table, to put in every page table. */
static u64_t kernel_pml4[NR_ENTRIES - KERN_PML4E];

/* The PML4 entry with the mappings made on behalf of the kernel. */
static u64_t kernmap_pml4e;

#define MAX_KERNMAPPINGS 10
static struct {
	phys_bytes	phys_addr;	/* Physical addr. */
	phys_bytes	len;		/* Length in bytes. */
	vir_bytes	vir_addr;	/* Address in every address space. */
	int		flags;
} kern_mappings[MAX_KERNMAPPINGS];
int kernmappings = 0;

static int pt_init_done;

/* Flags of entries that point to a lower level table: always 'present,'
 * 'writable,' and 'user accessible,' relying on the PTE for protection.
 */
#define TABLE_FLAGS	(ARCH_VM_PDE_PRESENT | ARCH_VM_PTE_USER | ARCH_VM_PTE_RW)

/* Levels of the tree, counted from the leaves. */
#define LEVEL_PT	0
#define LEVEL_PD	1
#define LEVEL_PDPT	2
#define LEVEL_PML4	3

#define LEVEL_SHIFT(l)	(I386_VM_PT_ENT_SHIFT + 9 * (l))
#define LEVEL_INDEX(v, l) (((v) >> LEVEL_SHIFT(l)) & I386_VM_PT_ENT_MASK)
#define LEVEL_SPAN(l)	((vir_bytes) 1 << LEVEL_SHIFT(l))

/*===========================================================================*
 *				phys2vir		     		     *
 *===========================================================================*/
static void *phys2vir(phys_bytes phys)
{
/* Where in our address space is this physical memory? */
	return (void *) (X86_64_VM_DMAP_BASE + phys);
}

static phys_bytes vir2phys(void *vir)
{
	vir_bytes v = (vir_bytes) vir;

	assert(v >= X86_64_VM_DMAP_BASE);
	assert(v < X86_64_VM_DMAP_BASE + X86_64_PML4_SPAN);
	return v - X86_64_VM_DMAP_BASE;
}

static int is_dmapaddr(vir_bytes v)
{
	return v >= X86_64_VM_DMAP_BASE &&
		v < X86_64_VM_DMAP_BASE + X86_64_PML4_SPAN;
}

/*===========================================================================*
 *				vm_freepages		     		     *
 *===========================================================================*/
void vm_freepages(vir_bytes vir, int pages)
{
	assert(!(vir % VM_PAGE_SIZE));

	free_mem(ABS2CLICK(vir2phys((void *) vir)), pages);
	vm_self_pages -= pages;
}

/*===========================================================================*
 *				vm_mappages		     		     *
 *===========================================================================*/
void *vm_mappages(phys_bytes p, int pages)
{
/* All of physical memory is mapped already. */
	return phys2vir(p);
}

/*===========================================================================*
 *				vm_allocpage		     		     *
 *===========================================================================*/
void *vm_allocpages(phys_bytes *phys, int reason, int pages)
{
/* Allocate a page for use by VM itself. */
	phys_bytes newpage;

	assert(reason >= 0 && reason < VMP_CATEGORIES);
	assert(pages > 0);

	/* Allocate page of memory for use by VM. As VM
	 * is trusted, we don't have to pre-clear it.
	 */
	if((newpage = alloc_mem(pages, 0)) == NO_MEM) {
		printf("VM: vm_allocpage: alloc_mem failed\n");
		return NULL;
	}

	*phys = CLICK2ABS(newpage);
	vm_self_pages += pages;

	return phys2vir(*phys);
}

void *vm_allocpage(phys_bytes *phys, int reason)
{
	return vm_allocpages(phys, reason, 1);
}

/*===========================================================================*
 *				vm_pagelock		     		     *
 *===========================================================================*/
void vm_pagelock(void *vir, int lockflag)
{
/* Mark a page allocated by vm_allocpage() unwritable, i.e. only for VM.
 * Pages in the direct map are part of 2MB mappings, of which the protection
 * can not be changed for one page; this debugging aid is not available.
 */
}

/*===========================================================================*
 *				pt_table		     		     *
 *===========================================================================*/
static u64_t *pt_table(u64_t ent)
{
/* The lower level table that an entry points to. */
	assert(ent & ARCH_VM_PDE_PRESENT);
	assert(!(ent & ARCH_VM_BIGPAGE));
	return phys2vir(ent & ARCH_VM_ADDR_MASK);
}

/*===========================================================================*
 *				pt_newtable		     		     *
 *===========================================================================*/
static u64_t *pt_newtable(phys_bytes *phys)
{
/* Allocate an empty table of any level. */
	u64_t *table;

	if(!(table = vm_allocpage(phys, VMP_PAGETABLE)))
		return NULL;
	memset(table, 0, VM_PAGE_SIZE);

	return table;
}

/*===========================================================================*
 *				pt_pte			     		     *
 *===========================================================================*/
static u64_t *pt_pte(pt_t *pt, vir_bytes v, int create)
{
/* Find the page table entry for a virtual address. If the page table or
 * the tables above it are not there, they are made if 'create' is set, and
 * NULL is returned otherwise (or if there is no memory).
 */
	u64_t *table = pt->pt_dir;
	int level;

	assert(pt->pt_dir);

	for(level = LEVEL_PML4; level > LEVEL_PT; level--) {
		u64_t *ent = &table[LEVEL_INDEX(v, level)];

		if(!(*ent & ARCH_VM_PDE_PRESENT)) {
			phys_bytes phys;

			if(!create) return NULL;
			assert(!*ent);
			if(!pt_newtable(&phys)) return NULL;
			*ent = (phys & ARCH_VM_ADDR_MASK) | TABLE_FLAGS;
		}

		table = pt_table(*ent);
	}

	return &table[LEVEL_INDEX(v, LEVEL_PT)];
}

/*===========================================================================*
 *				vm_addrok		     		     *
 *===========================================================================*/
int vm_addrok(void *vir, int writeflag)
{
	pt_t *pt = &vmprocess->vm_pt;
	vir_bytes v = (vir_bytes) vir;
	u64_t *pte;

	/* The direct map is always there, and writable. */
	if(is_dmapaddr(v))
		return 1;

	if(!(pte = pt_pte(pt, v, 0))) {
		printf("addr not ok: no page table for 0x%lx\n", v);
		return 0;
	}

	if(!(*pte & ARCH_VM_PTE_PRESENT)) {
		printf("addr not ok: missing pte for 0x%lx\n", v);
		return 0;
	}

	if(writeflag && !(*pte & ARCH_VM_PTE_RW)) {
		printf("addr not ok: 0x%lx present but unwritable\n", v);
		return 0;
	}

	return 1;
}

/*===========================================================================*
 *			    pt_ptalloc_in_range		     		     *
 *===========================================================================*/
int pt_ptalloc_in_range(pt_t *pt, vir_bytes start, vir_bytes end,
	u32_t flags, int verify)
{
/* Allocate all the page tables in the range specified. */
	vir_bytes v;

	assert(start < end);
	assert(end <= X86_64_VM_USER_TOP);

	/* Visit every page table (which maps 2MB) the range touches. */
	for(v = rounddown(start, ARCH_BIG_PAGE_SIZE); v < end;
		v += ARCH_BIG_PAGE_SIZE) {
		if(!pt_pte(pt, v, !verify)) {
			if(verify) {
				printf("pt_ptalloc_in_range: no pt for 0x%lx\n",
					v);
				return EFAULT;
			}
			/* Couldn't do (complete) mapping.
			 * Don't bother freeing any previously
			 * allocated page tables, they're
			 * still writable, don't point to nonsense,
			 * and the tree is in a consistent state.
			 */
			return ENOMEM;
		}
	}

	return OK;
}

static const char *ptestr(u64_t pte)
{
#define FLAG(constant, name) {						\
	if(pte & (constant)) { strcat(str, name); strcat(str, " "); }	\
}

	static char str[30];
	if(!(pte & ARCH_VM_PTE_PRESENT)) {
		return "not present";
	}
	str[0] = '\0';
	FLAG(ARCH_VM_PTE_RW, "W");
	FLAG(ARCH_VM_PTE_USER, "U");
	FLAG(I386_VM_PWT, "PWT");
	FLAG(I386_VM_PCD, "PCD");
	FLAG(I386_VM_ACC, "ACC");
	FLAG(I386_VM_DIRTY, "DIRTY");
	FLAG(I386_VM_PS, "PS");
	FLAG(I386_VM_GLOBAL, "G");

	return str;
}

/*===========================================================================*
 *				pt_nextpresent		     		     *
 *===========================================================================*/
static u64_t *pt_nextpresent(pt_t *pt, vir_bytes *vp, vir_bytes end)
{
/* Find the first present page table entry at or after *vp and before
 * 'end'; whole absent subtrees are skipped. Returns NULL if there is none,
 * otherwise the entry, with *vp set to its address.
 */
	vir_bytes v = *vp;

	while(v < end) {
		u64_t *table = pt->pt_dir;
		int level;

		for(level = LEVEL_PML4; level > LEVEL_PT; level--) {
			u64_t ent = table[LEVEL_INDEX(v, level)];

			if(!(ent & ARCH_VM_PDE_PRESENT) ||
				(ent & ARCH_VM_BIGPAGE))
				break;
			table = pt_table(ent);
		}

		if(level == LEVEL_PT) {
			u64_t *pte = &table[LEVEL_INDEX(v, LEVEL_PT)];

			if(*pte & ARCH_VM_PTE_PRESENT) {
				*vp = v;
				return pte;
			}
		}

		/* Nothing here; on to what the next entry of the level we
		 * got stuck at covers.
		 */
		v = (v & ~(LEVEL_SPAN(level) - 1)) + LEVEL_SPAN(level);
		if(v == 0) break;	/* wrapped */
	}

	return NULL;
}

/*===========================================================================*
 *			     pt_map_in_range		     		     *
 *===========================================================================*/
int pt_map_in_range(struct vmproc *src_vmp, struct vmproc *dst_vmp,
	vir_bytes start, vir_bytes end)
{
/* Transfer all the mappings from the pt of the source process to the pt of
 * the destination process in the range specified.
 */
	vir_bytes viraddr;
	pt_t *pt, *dst_pt;
	u64_t *pte, *dst_pte;

	pt = &src_vmp->vm_pt;
	dst_pt = &dst_vmp->vm_pt;

	end = end ? end : VM_DATATOP;
	assert(start % VM_PAGE_SIZE == 0);
	assert(end % VM_PAGE_SIZE == 0);
	assert(start <= end);

	/* Scan all page-table entries in the range. */
	for(viraddr = start;
		(pte = pt_nextpresent(pt, &viraddr, end)) != NULL;
		viraddr += VM_PAGE_SIZE) {
		/* Transfer the mapping. */
		if(!(dst_pte = pt_pte(dst_pt, viraddr, 1)))
			return ENOMEM;
		*dst_pte = *pte;
	}

	return OK;
}

/*===========================================================================*
 *				pt_ptmap		     		     *
 *===========================================================================*/
int pt_ptmap(struct vmproc *src_vmp, struct vmproc *dst_vmp)
{
/* Transfer mappings to page dir and page tables from source process and
 * destination process. Page tables are not mapped anywhere but in the
 * direct map, which every VM instance has (see pt_bind), so there is
 * nothing to transfer.
 */
	return OK;
}

void pt_clearmapcache(void)
{
	/* Make sure kernel will invalidate tlb when using current
	 * pagetable (i.e. vm's) to make new mappings before new cr3
	 * is loaded.
	 */
	if(sys_vmctl(SELF, VMCTL_CLEARMAPCACHE, 0) != OK)
		panic("VMCTL_CLEARMAPCACHE failed");
}

int pt_writable(struct vmproc *vmp, vir_bytes v)
{
	u64_t *pte;
	pt_t *pt = &vmp->vm_pt;
	assert(!(v % VM_PAGE_SIZE));

	pte = pt_pte(pt, v, 0);
	assert(pte);

	return((*pte & PTF_WRITE) ? 1 : 0);
}

/*===========================================================================*
 *				pt_writemap		     		     *
 *===========================================================================*/
int pt_writemap(struct vmproc * vmp,
			pt_t *pt,
			vir_bytes v,
			phys_bytes physaddr,
			size_t bytes,
			u32_t flags,
			u32_t writemapflags)
{
/* Write mapping into page table. Allocate a new page table if necessary. */
	size_t p, pages;
	int verify = 0;
	int ret = OK;

	if(writemapflags & WMF_VERIFY)
		verify = 1;

	assert(!(bytes % VM_PAGE_SIZE));
	assert(!(flags & ~(PTF_ALLFLAGS)));

	pages = bytes / VM_PAGE_SIZE;

	/* MAP_NONE means to clear the mapping. It doesn't matter
	 * what's actually written into the PTE if PRESENT
	 * isn't on, so we can just write MAP_NONE into it.
	 */
	assert(physaddr == MAP_NONE || (flags & ARCH_VM_PTE_PRESENT));
	assert(physaddr != MAP_NONE || !flags);

	/* First make sure all the necessary page tables are allocated,
	 * before we start writing in any of them, because it's a pain
	 * to undo our work properly.
	 */
	ret = pt_ptalloc_in_range(pt, v, v + VM_PAGE_SIZE*pages, flags, verify);
	if(ret != OK) {
		printf("VM: writemap: pt_ptalloc_in_range failed\n");
		return ret;
	}

	/* Now write in them. */
	for(p = 0; p < pages; p++) {
		u64_t entry, *pte;

		assert(!(v % VM_PAGE_SIZE));

		/* Page table has to be there. */
		pte = pt_pte(pt, v, 0);
		assert(pte);

		if(writemapflags & (WMF_WRITEFLAGSONLY|WMF_FREE)) {
			physaddr = *pte & ARCH_VM_ADDR_MASK;
		}

		if(writemapflags & WMF_FREE) {
			free_mem(ABS2CLICK(physaddr), 1);
		}

		/* Entry we will write. */
		entry = (physaddr & ARCH_VM_ADDR_MASK) | flags;

		if(verify) {
			u64_t maskedentry;
			maskedentry = *pte;
			maskedentry &= ~(I386_VM_ACC|I386_VM_DIRTY);
			/* Verify pagetable entry. */
			if(entry & ARCH_VM_PTE_RW) {
				/* If we expect a writable page, allow a readonly page. */
				maskedentry |= ARCH_VM_PTE_RW;
			}
			if(maskedentry != entry) {
				printf("pt_writemap: mismatch: ");
				if((entry & ARCH_VM_ADDR_MASK) !=
					(maskedentry & ARCH_VM_ADDR_MASK)) {
					printf("pt_writemap: physaddr mismatch (0x%lx, 0x%lx); ",
						(long)entry, (long)maskedentry);
				} else printf("phys ok; ");
				printf(" flags: found %s; ", ptestr(*pte));
				printf(" masked %s; ", ptestr(maskedentry));
				printf(" expected %s\n", ptestr(entry));
				printf("found 0x%lx, wanted 0x%lx\n",
					*pte, entry);
				return EFAULT;
			}
		} else {
			/* Write pagetable entry. */
			*pte = entry;
		}

		physaddr += VM_PAGE_SIZE;
		v += VM_PAGE_SIZE;
	}

	return ret;
}

/*===========================================================================*
 *				pt_checkrange		     		     *
 *===========================================================================*/
int pt_checkrange(pt_t *pt, vir_bytes v,  size_t bytes,
	int write)
{
	size_t p, pages;

	assert(!(bytes % VM_PAGE_SIZE));

	pages = bytes / VM_PAGE_SIZE;

	for(p = 0; p < pages; p++) {
		u64_t *pte;

		assert(!(v % VM_PAGE_SIZE));

		/* Page table has to be there. */
		if(!(pte = pt_pte(pt, v, 0)))
			return EFAULT;

		if(!(*pte & ARCH_VM_PTE_PRESENT)) {
			return EFAULT;
		}

		if(write && !(*pte & ARCH_VM_PTE_RW)) {
			return EFAULT;
		}

		v += VM_PAGE_SIZE;
	}

	return OK;
}

/*===========================================================================*
 *				pt_new			     		     *
 *===========================================================================*/
int pt_new(pt_t *pt)
{
/* Allocate a pagetable root, or reuse the one this pt had before, and
 * empty it (indicating no page tables are allocated).
 */
	int r;

	/* Don't ever re-allocate/re-move a certain process slot's
	 * top level table once it's been created. This is a fraction
	 * faster.
	 */
        if(!pt->pt_dir &&
          !(pt->pt_dir = vm_allocpage(&pt->pt_dir_phys, VMP_PAGEDIR))) {
		return ENOMEM;
	}

	assert(!(pt->pt_dir_phys % VM_PAGE_SIZE));

	memset(pt->pt_dir, 0, VM_PAGE_SIZE);

	/* Where to start looking for free virtual address space? */
	pt->pt_virtop = 0;

        /* Map in kernel. */
        if((r=pt_mapkernel(pt)) != OK)
		return r;

	return OK;
}

/*===========================================================================*
 *				pt_copytree		     		     *
 *===========================================================================*/
static u64_t pt_copytree(u64_t ent, int level)
{
/* Make a copy of the table that 'ent', an entry of a table of the given
 * level, points to, and of everything below it; return the entry for the
 * copy. Mappings of pages themselves are copied as they are.
 */
	u64_t *src, *dst;
	phys_bytes phys;
	int i;

	assert(level > LEVEL_PT);

	src = pt_table(ent);
	if(!(dst = pt_newtable(&phys)))
		panic("pt_copytree: out of memory");

	for(i = 0; i < NR_ENTRIES; i++) {
		if(!(src[i] & ARCH_VM_PDE_PRESENT))
			continue;
		if(level - 1 == LEVEL_PT)
			dst[i] = src[i];
		else
			dst[i] = pt_copytree(src[i], level - 1);
	}

	return (phys & ARCH_VM_ADDR_MASK) | (ent & ~ARCH_VM_ADDR_MASK);
}

/*===========================================================================*
 *                              pt_init                                      *
 *===========================================================================*/
void pt_init(void)
{
        pt_t *newpt;
	u64_t *bootpt;
	phys_bytes mypdbr; /* Page table root (cr3) value */
	phys_bytes addr, len;
	vir_bytes vir;
	int flags, i, pindex = 0;

	/* Get our current page table, the one the kernel made to get us
	 * going, so we can see it. It has the direct map, so we can.
	 */
	if(sys_vmctl_get_pdbr(SELF, &mypdbr) != OK)
		panic("VM: sys_vmctl_get_pdbr failed");
	bootpt = phys2vir(mypdbr & ARCH_VM_ADDR_MASK);

	/* The kernel half is to be part of every page table. */
	for(i = KERN_PML4E; i < NR_ENTRIES; i++)
		kernel_pml4[i - KERN_PML4E] = bootpt[i];

	/* Allright. Now. We have to make our own page table. It is to
	 * contain what the kernel mapped for us (our text, data, bss and
	 * stack), but in page tables that we allocated and may free: so
	 * copy those. The direct map is the kernel's; we share it.
	 */
        newpt = &vmprocess->vm_pt;
	memset(newpt, 0, sizeof(*newpt));
        if(!(newpt->pt_dir = pt_newtable(&newpt->pt_dir_phys)))
		panic("vm: no memory for page table");

	for(i = 0; i < X86_64_VM_USER_PML4_ENTRIES; i++) {
		if(!(bootpt[i] & ARCH_VM_PDE_PRESENT)) continue;
		if(i == X86_64_VM_DMAP_PML4E)
			newpt->pt_dir[i] = bootpt[i];
		else
			newpt->pt_dir[i] = pt_copytree(bootpt[i], LEVEL_PML4);
	}

	/* The kernel has mappings of its own that it wants in every
	 * address space. They all go under one PML4 entry, the tables
	 * behind which we make once, here, in our own page table.
	 */
	vir = X86_64_VM_KERNMAP_BASE;
	assert(!newpt->pt_dir[X86_64_VM_KERNMAP_PML4E]);
	while(sys_vmctl_get_mapping(pindex, &addr, &len, &flags) == OK)  {
		if(pindex >= MAX_KERNMAPPINGS)
			panic("VM: too many kernel mappings: %d", pindex);
		kern_mappings[pindex].phys_addr = addr;
		kern_mappings[pindex].len = len;
		kern_mappings[pindex].vir_addr = vir;
		kern_mappings[pindex].flags = ARCH_VM_PTE_PRESENT;
		if(flags & VMMF_UNCACHED)
			kern_mappings[pindex].flags |= PTF_NOCACHE;
		if(flags & VMMF_USER)
			kern_mappings[pindex].flags |= ARCH_VM_PTE_USER;
		if(flags & VMMF_WRITE)
			kern_mappings[pindex].flags |= ARCH_VM_PTE_RW;
		if(flags & VMMF_GLO)
			kern_mappings[pindex].flags |= I386_VM_GLOBAL;
		if(addr % VM_PAGE_SIZE)
			panic("VM: addr unaligned: %lu", addr);
		if(len % VM_PAGE_SIZE)
			panic("VM: len unaligned: %lu", len);
		if(pt_writemap(NULL, newpt, vir, addr, len,
			kern_mappings[pindex].flags, 0) != OK)
			panic("VM: kernel mapping failed");
		if(sys_vmctl_reply_mapping(pindex, vir) != OK)
			panic("VM: reply failed");
		vir += len;
		pindex++;
		kernmappings++;
	}
	assert(vir <= X86_64_VM_USER_TOP);
	kernmap_pml4e = newpt->pt_dir[X86_64_VM_KERNMAP_PML4E];

	/* Map in the kernel itself. */
	if(pt_mapkernel(newpt) != OK)
		panic("vm pt_mapkernel failed");

	/* Inform kernel vm has a newly built page table. */
	assert(vmproc[VM_PROC_NR].vm_endpoint == VM_PROC_NR);
	pt_bind(newpt, &vmproc[VM_PROC_NR]);

	pt_init_done = 1;

	/* Flush TLB just in case any of those mappings have been touched */
	if((sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed");
	}

        /* All OK. */
        return;
}

/*===========================================================================*
 *				pt_bind			     		     *
 *===========================================================================*/
int pt_bind(pt_t *pt, struct vmproc *who)
{
	/* Basic sanity checks. */
	assert(who);
	assert(who->vm_flags & VMF_INUSE);
	assert(pt);
	assert(pt->pt_dir);
	assert(!(pt->pt_dir_phys % VM_PAGE_SIZE));

	/* A VM instance (us, or our successor in a live update) needs the
	 * direct map of physical memory; nobody else may have it.
	 */
	if(who->vm_endpoint == VM_PROC_NR || (who->vm_flags & VMF_VM_INSTANCE))
		pt->pt_dir[X86_64_VM_DMAP_PML4E] =
			vmprocess->vm_pt.pt_dir[X86_64_VM_DMAP_PML4E];

	/* Tell kernel about new page table root. The kernel finds page
	 * tables through its own direct map; the virtual address only
	 * has to be there.
	 */
	return sys_vmctl_set_addrspace(who->vm_endpoint, pt->pt_dir_phys,
		pt->pt_dir);
}

/*===========================================================================*
 *				pt_freetree		     		     *
 *===========================================================================*/
static void pt_freetree(u64_t ent, int level)
{
/* Free the table that 'ent', an entry of a table of the given level,
 * points to, and all tables below it. Not the pages mapped by them.
 */
	u64_t *table = pt_table(ent);
	int i;

	assert(level > LEVEL_PT);

	if(level - 1 > LEVEL_PT) {
		for(i = 0; i < NR_ENTRIES; i++) {
			if(table[i] & ARCH_VM_PDE_PRESENT)
				pt_freetree(table[i], level - 1);
		}
	}

	vm_freepages((vir_bytes) table, 1);
}

/*===========================================================================*
 *				pt_free			     		     *
 *===========================================================================*/
void pt_free(pt_t *pt)
{
/* Free memory associated with this pagetable: the tables of the user half
 * that are its own. The top level table itself stays, for reuse.
 */
	int i;

	if(!pt->pt_dir)
		return;

	for(i = 0; i < X86_64_VM_USER_PML4_ENTRIES; i++) {
		if(!(pt->pt_dir[i] & ARCH_VM_PDE_PRESENT))
			continue;
		/* shared with everybody else */
		if(i == X86_64_VM_DMAP_PML4E || i == X86_64_VM_KERNMAP_PML4E)
			continue;
		pt_freetree(pt->pt_dir[i], LEVEL_PML4);
		pt->pt_dir[i] = 0;
	}

	pt_clearmapcache();

	return;
}

/*===========================================================================*
 *				pt_mapkernel		     		     *
 *===========================================================================*/
int pt_mapkernel(pt_t *pt)
{
	int i;

        /* Any page table needs to map in the kernel address space. */
	for(i = KERN_PML4E; i < NR_ENTRIES; i++)
		pt->pt_dir[i] = kernel_pml4[i - KERN_PML4E];

	/* Kernel also wants various mappings of its own. */
	pt->pt_dir[X86_64_VM_KERNMAP_PML4E] = kernmap_pml4e;

	return OK;
}

/*===========================================================================*
 *				pt_assert		     		     *
 *===========================================================================*/
void pt_assert(pt_t *pt)
{
	assert(pt->pt_dir);
	assert(pt->pt_dir == phys2vir(pt->pt_dir_phys));
}

#if SANITYCHECKS
/*===========================================================================*
 *				pt_sanitycheck		     		     *
 *===========================================================================*/
void pt_sanitycheck(pt_t *pt, const char *file, int line)
{
/* Basic pt sanity check. */
	int slot;

	MYASSERT(pt);
	MYASSERT(pt->pt_dir);
	MYASSERT(pt->pt_dir_phys);

	for(slot = 0; slot < ELEMENTS(vmproc); slot++) {
		if(pt == &vmproc[slot].vm_pt)
			break;
	}

	if(slot >= ELEMENTS(vmproc)) {
		panic("pt_sanitycheck: passed pt not in any proc");
	}

	MYASSERT(usedpages_add(pt->pt_dir_phys, VM_PAGE_SIZE) == OK);
}
#endif

int get_vm_self_pages(void) { return vm_self_pages; }
