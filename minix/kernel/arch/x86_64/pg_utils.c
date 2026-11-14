
#include <minix/cpufeature.h>

#include <assert.h>
#include "kernel/kernel.h"
#include "arch_proto.h"

#include <string.h>

/* These are set/computed in kernel.lds. */
extern char _kern_vir_base, _kern_phys_base, _kern_size;

/* Retrieve the absolute values to something we can use. */
static phys_bytes kern_vir_start = (phys_bytes) &_kern_vir_base;
static phys_bytes kern_phys_start = (phys_bytes) &_kern_phys_base;
static phys_bytes kern_kernlen = (phys_bytes) &_kern_size;

/* The bootstrap page table.  It consists of:
 *  - the direct map of physical memory (PML4 slot KERNEL_DMAP_PML4E, and,
 *    user accessible, slot VM_DMAP_PML4E for VM);
 *  - the kernel image in the top 2GB (PML4 slot KERNEL_IMAGE_PML4E);
 *  - optionally a 1:1 mapping of the first 4GB (PML4 slot 0), which is
 *    only there while the kernel still runs at its load address;
 *  - whatever pg_map() adds in the user half: the VM server.
 * All of it is mapped with 2MB pages, except what pg_map() adds.
 */
static u64_t pml4[512] __aligned(4096);
static u64_t pdpt_dmap[512] __aligned(4096);
static u64_t pd_dmap[KERNEL_DMAP_GB][512] __aligned(4096);
static u64_t pdpt_ident[512] __aligned(4096);
static u64_t pdpt_kern[512] __aligned(4096);
static u64_t pd_kern[512] __aligned(4096);

#define IDENT_GB	4	/* size of the 1:1 mapping */

/* Kernel virtual address of a page table page of ours, given its physical
 * address; the inverse of vir2phys() (which is the identity as long as we
 * run at the load address).
 */
static u64_t *phys2pt(phys_bytes ph)
{
	return (u64_t *) (ph - vir2phys((void *) 0));
}

void print_memmap(kinfo_t *cbi)
{
        int m;
        assert(cbi->mmap_size < MAXMEMMAP);
        for(m = 0; m < cbi->mmap_size; m++) {
		phys_bytes addr = cbi->memmap[m].mm_base_addr, endit = cbi->memmap[m].mm_base_addr + cbi->memmap[m].mm_length;
                printf("%012lx-%012lx ",addr, endit);
        }
        printf("\nsize %d\n", cbi->mmap_size);
}

void cut_memmap(kinfo_t *cbi, phys_bytes start, phys_bytes end)
{
        int m;
        phys_bytes o;

        if((o=start % I386_PAGE_SIZE))
                start -= o;
        if((o=end % I386_PAGE_SIZE))
                end += I386_PAGE_SIZE - o;

	assert(kernel_may_alloc);

        for(m = 0; m < cbi->mmap_size; m++) {
                phys_bytes substart = start, subend = end;
                phys_bytes memaddr = cbi->memmap[m].mm_base_addr,
                        memend = cbi->memmap[m].mm_base_addr + cbi->memmap[m].mm_length;

                /* adjust cut range to be a subset of the free memory */
                if(substart < memaddr) substart = memaddr;
                if(subend > memend) subend = memend;
                if(substart >= subend) continue;

                /* if there is any overlap, forget this one and add
                 * 1-2 subranges back
                 */
                cbi->memmap[m].mm_base_addr = cbi->memmap[m].mm_length = 0;
                if(substart > memaddr)
                        add_memmap(cbi, memaddr, substart-memaddr);
                if(subend < memend)
                        add_memmap(cbi, subend, memend-subend);
        }
}

phys_bytes alloc_lowest(kinfo_t *cbi, phys_bytes len)
{
	/* Allocate the lowest physical page we have. */
	int m;
#define EMPTY (~(phys_bytes)0)
	phys_bytes lowest = EMPTY;
	assert(len > 0);
	len = roundup(len, I386_PAGE_SIZE);

	assert(kernel_may_alloc);

	for(m = 0; m < cbi->mmap_size; m++) {
		if(cbi->memmap[m].mm_length < len) continue;
		if(cbi->memmap[m].mm_base_addr < lowest) lowest = cbi->memmap[m].mm_base_addr;
	}
	assert(lowest != EMPTY);
	cut_memmap(cbi, lowest, len);
	cbi->kernel_allocated_bytes_dynamic += len;
	return lowest;
}

void add_memmap(kinfo_t *cbi, u64_t addr, u64_t len)
{
        int m;
#define LIMIT KERNEL_DMAP_SIZE
        /* Truncate available memory at the size of the direct map,
         * as the kernel can not get at anything beyond it.
         */
        if(addr > LIMIT) return;
        if(addr + len > LIMIT) {
                len -= (addr + len - LIMIT);
        }
        assert(cbi->mmap_size < MAXMEMMAP);
        if(len == 0) return;
	addr = roundup(addr, I386_PAGE_SIZE);
	len = rounddown(len, I386_PAGE_SIZE);

	assert(kernel_may_alloc);

        for(m = 0; m < MAXMEMMAP; m++) {
		phys_bytes highmark;
                if(cbi->memmap[m].mm_length) continue;
                cbi->memmap[m].mm_base_addr = addr;
                cbi->memmap[m].mm_length = len;
                cbi->memmap[m].mm_type = MULTIBOOT_MEMORY_AVAILABLE;
                if(m >= cbi->mmap_size)
                        cbi->mmap_size = m+1;
		highmark = addr + len;
		if(highmark > cbi->mem_high_phys) {
			cbi->mem_high_phys = highmark;
		}

                return;
        }

        panic("no available memmap slot");
}

u64_t *alloc_pagetable(phys_bytes *ph)
{
	u64_t *ret;
#define PG_PAGETABLES 32
	static u64_t pagetables[PG_PAGETABLES][512]  __aligned(4096);
	static int pt_inuse = 0;
	if(pt_inuse >= PG_PAGETABLES) panic("no more pagetables");
	assert(sizeof(pagetables[pt_inuse]) == I386_PAGE_SIZE);
	ret = pagetables[pt_inuse++];
	memset(ret, 0, I386_PAGE_SIZE);
	*ph = vir2phys(ret);
	return ret;
}

#define PAGE_KB (I386_PAGE_SIZE / 1024)

phys_bytes pg_alloc_page(kinfo_t *cbi)
{
	int m;
	multiboot_memory_map_t *mmap;

	assert(kernel_may_alloc);

	for(m = cbi->mmap_size-1; m >= 0; m--) {
		mmap = &cbi->memmap[m];
		if(!mmap->mm_length) continue;
		assert(mmap->mm_length > 0);
		assert(!(mmap->mm_length % I386_PAGE_SIZE));
		assert(!(mmap->mm_base_addr % I386_PAGE_SIZE));

		mmap->mm_length -= I386_PAGE_SIZE;

                cbi->kernel_allocated_bytes_dynamic += I386_PAGE_SIZE;

		return mmap->mm_base_addr + mmap->mm_length;
	}

	panic("can't find free memory");
}

static void pg_dmap(kinfo_t *cbi)
{
	int gb, i;

	/* We map memory that does not correspond to physical memory
	 * as non-cacheable. Make sure we know what it is.
	 */
	assert(cbi->mem_high_phys);

	/* The entries below the PML4 are user accessible, so that the
	 * same tables can serve VM's view of the direct map; it is the
	 * PML4 entry that decides who gets to use them.
	 */
	for(gb = 0; gb < KERNEL_DMAP_GB; gb++) {
		for(i = 0; i < 512; i++) {
			u64_t flags = I386_VM_PRESENT | I386_VM_BIGPAGE
				| I386_VM_USER | I386_VM_WRITE;
			phys_bytes phys = ((phys_bytes) gb << 30) +
				(phys_bytes) i * I386_BIG_PAGE_SIZE;
			if((cbi->mem_high_phys & I386_VM_ADDR_MASK_BIG)
				<= phys) {
				flags |= I386_VM_PWT | I386_VM_PCD;
			}
			pd_dmap[gb][i] = phys | flags;
		}
		pdpt_dmap[gb] = vir2phys(pd_dmap[gb]) | I386_VM_PRESENT
			| I386_VM_USER | I386_VM_WRITE;
	}

	pml4[KERNEL_DMAP_PML4E] = vir2phys(pdpt_dmap) | I386_VM_PRESENT
		| I386_VM_WRITE;
	pml4[VM_DMAP_PML4E] = vir2phys(pdpt_dmap) | I386_VM_PRESENT
		| I386_VM_USER | I386_VM_WRITE;
}

void pg_identity(kinfo_t *cbi)
{
	int gb;

	/* The direct map is always part of the page table. */
	pg_dmap(cbi);

	/* Set up an identity mapping of low memory. It can share the
	 * page directories of the direct map, as these map the same
	 * physical memory in the same order.
	 */
	for(gb = 0; gb < IDENT_GB; gb++)
		pdpt_ident[gb] = pdpt_dmap[gb];
	pml4[0] = vir2phys(pdpt_ident) | I386_VM_PRESENT
		| I386_VM_USER | I386_VM_WRITE;
}

void pg_noidentity(kinfo_t *cbi)
{
	/* Same as pg_identity(), but without the identity mapping: the
	 * user half of the address space is left empty.
	 */
	pg_dmap(cbi);
}

int pg_mapkernel(void)
{
	int pde;
	phys_bytes mapped = 0;

        assert(!(kern_vir_start % I386_BIG_PAGE_SIZE));
        assert(!(kern_phys_start % I386_BIG_PAGE_SIZE));
	assert(kern_vir_start - kern_phys_start == KERNEL_IMAGE_BASE);

	/* Map physical memory from 0 up to the end of the kernel at
	 * KERNEL_IMAGE_BASE.
	 */
	for(pde = 0; mapped < kern_phys_start + kern_kernlen; pde++) {
		assert(pde < 512);
	        pd_kern[pde] = mapped | I386_VM_PRESENT |
			I386_VM_BIGPAGE | I386_VM_WRITE;
		mapped += I386_BIG_PAGE_SIZE;
	}

	/* The usermapped part of the kernel is made accessible to
	 * processes by VM, through mappings of its own; nothing in here
	 * is user accessible.
	 */
	pdpt_kern[KERNEL_IMAGE_PDPTE] = vir2phys(pd_kern) | I386_VM_PRESENT
		| I386_VM_WRITE;
	pml4[KERNEL_IMAGE_PML4E] = vir2phys(pdpt_kern) | I386_VM_PRESENT
		| I386_VM_WRITE;

	return 0;	/* there are no free pdes to hand out */
}

void vm_enable_paging(void)
{
        u64_t cr0, cr4;

	/* Long mode implies paging and PAE; they were enabled by the
	 * startup code. Turn on what we want in addition to that.
	 */
        cr0= read_cr0();
        cr4= read_cr4();

	assert(cr0 & I386_CR0_PG);
	assert(cr4 & I386_CR4_PAE);

	/* The kernel honours read-only pages too. */
        cr0 |= I386_CR0_WP;
        write_cr0(cr0);

	/* Every long mode CPU has global pages and FXSAVE/SSE. */
        cr4 |= I386_CR4_PGE | I386_CR4_OSFXSR | I386_CR4_OSXMMEXCPT;
        write_cr4(cr4);
}

phys_bytes pg_load(void)
{
	phys_bytes phpagedir = vir2phys(pml4);
        write_cr3(phpagedir);
	return phpagedir;
}

void pg_clear(void)
{
	memset(pml4, 0, sizeof(pml4));
	memset(pdpt_dmap, 0, sizeof(pdpt_dmap));
	memset(pdpt_ident, 0, sizeof(pdpt_ident));
	memset(pdpt_kern, 0, sizeof(pdpt_kern));
	memset(pd_kern, 0, sizeof(pd_kern));
}

phys_bytes pg_rounddown(phys_bytes b)
{
	phys_bytes o;
	if(!(o = b % I386_PAGE_SIZE))
		return b;
	return b  - o;
}

/* Return the table the given entry of a higher level table points to,
 * allocating it if the entry is still empty.
 */
static u64_t *pg_nextlevel(u64_t *table, int idx)
{
	phys_bytes ph;

	if(!(table[idx] & I386_VM_PRESENT)) {
		(void) alloc_pagetable(&ph);
		table[idx] = (ph & I386_VM_ADDR_MASK)
			| I386_VM_PRESENT | I386_VM_USER | I386_VM_WRITE;
	}
	assert(!(table[idx] & I386_VM_BIGPAGE));

	return phys2pt(table[idx] & I386_VM_ADDR_MASK);
}

void pg_map(phys_bytes phys, vir_bytes vaddr, vir_bytes vaddr_end,
	kinfo_t *cbi)
{
	u64_t *pdpt, *pd, *pt;

	assert(kernel_may_alloc);

	if(phys == PG_ALLOCATEME) {
		assert(!(vaddr % I386_PAGE_SIZE));
	} else  {
		assert((vaddr % I386_PAGE_SIZE) == (phys % I386_PAGE_SIZE));
		vaddr = pg_rounddown(vaddr);
		phys = pg_rounddown(phys);
	}

	/* Only the user half, and not VM's direct map either. */
	assert(vaddr_end <= VM_DMAP_BASE || vaddr >= VM_DMAP_BASE +
		X86_64_PML4_SPAN);
	assert(vaddr_end <= X86_64_VM_USER_TOP);

	while(vaddr < vaddr_end) {
		phys_bytes source = phys;
		assert(!(vaddr % I386_PAGE_SIZE));
		if(phys == PG_ALLOCATEME) {
			source = pg_alloc_page(cbi);
		} else {
			assert(!(phys % I386_PAGE_SIZE));
		}
		assert(!(source % I386_PAGE_SIZE));
		pdpt = pg_nextlevel(pml4, X86_64_VM_PML4E(vaddr));
		pd = pg_nextlevel(pdpt, X86_64_VM_PDPTE(vaddr));
		pt = pg_nextlevel(pd, I386_VM_PDE(vaddr));
		pt[I386_VM_PTE(vaddr)] = (source & I386_VM_ADDR_MASK) |
			I386_VM_PRESENT | I386_VM_USER | I386_VM_WRITE;
		vaddr += I386_PAGE_SIZE;
		if(phys != PG_ALLOCATEME)
			phys += I386_PAGE_SIZE;
	}
}

void pg_info(reg_t *pagedir_ph, u64_t **pagedir_v)
{
	*pagedir_ph = vir2phys(pml4);
	*pagedir_v = pml4;
}
