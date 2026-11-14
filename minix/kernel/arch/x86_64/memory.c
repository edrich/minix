
#include "kernel/kernel.h"
#include "kernel/vm.h"

#include <machine/vm.h>

#include <minix/syslib.h>
#include <minix/cpufeature.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>

#include <machine/vm.h>

#include "oxpcie.h"
#include "arch_proto.h"

#ifdef USE_APIC
#include "apic.h"
#ifdef USE_WATCHDOG
#include "kernel/watchdog.h"
#endif
#endif

phys_bytes video_mem_vaddr = 0;

#define HASPT(procptr) ((procptr)->p_seg.p_cr3 != 0)
/* Difference between where VM mapped the usermapped part of the kernel
 * into processes and where the kernel itself has it.
 */
static vir_bytes usermapped_offset;

/* The kernel gets at memory that is not part of the current address space,
 * such as that of other processes, through the direct map of physical
 * memory (see archconst.h). Unlike on i386 there is no need for temporary
 * mapping windows, and so there is no mapping cache to clear either.
 */
void mem_clear_mapcache(void)
{
}

/* Result of a page table walk on behalf of a copy. */
#define WALK_OK		0
#define WALK_FAULT	1	/* not mapped, or not with the needed access */

/*===========================================================================*
 *				pt_walk					     *
 *===========================================================================*/
static int pt_walk(const struct proc *proc, vir_bytes vaddr,
	phys_bytes *physical, u64_t *ptent, u64_t need)
{
/* Walk the page table of a process, in software, to translate a virtual
 * address. 'need' is the set of flags (I386_VM_USER, I386_VM_WRITE) that
 * must be set at every level of the walk for the access to be allowed, in
 * addition to the entries being present.
 */
	u64_t *table, ent;
	int level;
	static const int shift[4] = { X86_64_VM_PML4_ENT_SHIFT,
		X86_64_VM_PDPT_ENT_SHIFT, I386_VM_DIR_ENT_SHIFT,
		I386_VM_PT_ENT_SHIFT };

	/* Only canonical addresses can be mapped at all. */
	if (vaddr >= X86_64_VM_USER_TOP && vaddr < X86_64_VM_KERN_BASE)
		return WALK_FAULT;

	need |= I386_VM_PRESENT;
	table = PHYS_TO_DMAP(proc->p_seg.p_cr3 & I386_VM_ADDR_MASK);

	for (level = 0; level < 4; level++) {
		vir_bytes span = (vir_bytes) 1 << shift[level];

		ent = table[(vaddr >> shift[level]) & I386_VM_PT_ENT_MASK];
		if ((ent & need) != need)
			return WALK_FAULT;

		/* A leaf: the last level, or a big page (2MB, 1GB). */
		if (level == 3 || (ent & I386_VM_BIGPAGE)) {
			assert(level > 0);
			*physical = (ent & I386_VM_ADDR_MASK & ~(span - 1)) |
				(vaddr & (span - 1));
			if (ptent) *ptent = ent;
			return WALK_OK;
		}

		table = PHYS_TO_DMAP(ent & I386_VM_ADDR_MASK);
	}

	NOT_REACHABLE;
	return WALK_FAULT;
}

/*===========================================================================*
 *				copy_resolve				     *
 *===========================================================================*/
static int copy_resolve(struct proc *pr, vir_bytes addr, int writing,
	vir_bytes *bytes, char **ptr)
{
/* Find out where the kernel can get at 'addr' of process 'pr' (NULL for
 * physical memory). Sets *ptr to a kernel virtual address and limits
 * *bytes to what is contiguously available there. Fails if the address is
 * not mapped in the process with the access that is needed, in which case
 * VM may be able to do something about it, exactly as if a page fault had
 * occurred.
 */
	phys_bytes phys;

	if (!pr) {
		/* Physical memory. */
		if (addr >= KERNEL_DMAP_SIZE ||
			*bytes > KERNEL_DMAP_SIZE - addr)
			return WALK_FAULT;
		*ptr = PHYS_TO_DMAP(addr);
		return WALK_OK;
	}

	if (iskernelp(pr)) {
		/* Kernel memory is always there, in every address space. */
		*ptr = (char *) addr;
		return WALK_OK;
	}

	assert(HASPT(pr));

	if (pt_walk(pr, addr, &phys, NULL,
		I386_VM_USER | (writing ? I386_VM_WRITE : 0)) != WALK_OK)
		return WALK_FAULT;

	/* One page at a time. */
	*bytes = MIN(*bytes, I386_PAGE_SIZE - (addr % I386_PAGE_SIZE));
	*ptr = PHYS_TO_DMAP(phys);

	return WALK_OK;
}

/*===========================================================================*
 *                           check_resumed_caller                            *
 *===========================================================================*/
static int check_resumed_caller(struct proc *caller)
{
	/* Returns the result from VM if caller was resumed, otherwise OK. */
	if (caller && (caller->p_misc_flags & MF_KCALL_RESUME)) {
		assert(caller->p_vmrequest.vmresult != VMSUSPEND);
		return caller->p_vmrequest.vmresult;
	}

	return OK;
}
  
/*===========================================================================*
 *				lin_lin_copy				     *
 *===========================================================================*/
static int lin_lin_copy(struct proc *srcproc, vir_bytes srclinaddr, 
	struct proc *dstproc, vir_bytes dstlinaddr, vir_bytes bytes)
{
	assert(get_cpulocal_var(ptproc));
	assert(get_cpulocal_var(proc_ptr));
	assert(read_cr3() == get_cpulocal_var(ptproc)->p_seg.p_cr3);

	if(srcproc) assert(!RTS_ISSET(srcproc, RTS_SLOT_FREE));
	if(dstproc) assert(!RTS_ISSET(dstproc, RTS_SLOT_FREE));
	if(srcproc) assert(!RTS_ISSET(srcproc, RTS_VMINHIBIT));
	if(dstproc) assert(!RTS_ISSET(dstproc, RTS_VMINHIBIT));

	while(bytes > 0) {
		char *srcptr, *dstptr;
		vir_bytes chunk = bytes;

		/* Check for overflow. */
		if (srclinaddr + chunk < srclinaddr) return EFAULT_SRC;
		if (dstlinaddr + chunk < dstlinaddr) return EFAULT_DST;

		/* Find both ends, a page at most. */
		if (copy_resolve(srcproc, srclinaddr, 0, &chunk, &srcptr) !=
			WALK_OK)
			return EFAULT_SRC;
		if (copy_resolve(dstproc, dstlinaddr, 1, &chunk, &dstptr) !=
			WALK_OK)
			return EFAULT_DST;

		/* Copy. Both ends were checked, so this can not fault. */
		memcpy(dstptr, srcptr, chunk);

		/* Update counter and addresses for next iteration, if any. */
		bytes -= chunk;
		srclinaddr += chunk;
		dstlinaddr += chunk;
	}

	return OK;
}

/*===========================================================================*
 *                              umap_virtual                                 *
 *===========================================================================*/
phys_bytes umap_virtual(
  register struct proc *rp,		/* pointer to proc table entry for process */
  int seg,				/* T, D, or S segment */
  vir_bytes vir_addr,			/* virtual address in bytes within the seg */
  vir_bytes bytes			/* # of bytes to be copied */
)
{
	phys_bytes phys = 0;

	if(vm_lookup(rp, vir_addr, &phys, NULL) != OK) {
		printf("SYSTEM:umap_virtual: vm_lookup of %s: seg 0x%x: 0x%lx failed\n", rp->p_name, seg, vir_addr);
		phys = 0;
	} else {
		if(phys == 0)
			panic("vm_lookup returned phys: 0x%lx",  phys);
	}

	if(phys == 0) {
		printf("SYSTEM:umap_virtual: lookup failed\n");
		return 0;
	}

	/* Now make sure addresses are contiguous in physical memory
	 * so that the umap makes sense.
	 */
	if(bytes > 0 && vm_lookup_range(rp, vir_addr, NULL, bytes) != bytes) {
		printf("umap_virtual: %s: %lu at 0x%lx (vir 0x%lx) not contiguous\n",
			rp->p_name, bytes, vir_addr, vir_addr);
		return 0;
	}

	/* phys must be larger than 0 (or the caller will think the call
	 * failed), and address must not cross a page boundary.
	 */
	assert(phys);

	return phys;
}


/*===========================================================================*
 *                              vm_lookup                                    *
 *===========================================================================*/
int vm_lookup(const struct proc *proc, const vir_bytes virtual,
 phys_bytes *physical, u32_t *ptent)
{
	u64_t ent;

	assert(proc);
	assert(physical);
	assert(!isemptyp(proc));
	assert(HASPT(proc));

	if (pt_walk(proc, virtual, physical, &ent, 0) != WALK_OK)
		return EFAULT;

	/* The callers only look at the flag bits of the entry, which
	 * are in its lower part.
	 */
	if(ptent) *ptent = (u32_t) ent;

	return OK;
}

/*===========================================================================*
 *				vm_lookup_range				     *
 *===========================================================================*/
size_t vm_lookup_range(const struct proc *proc, vir_bytes vir_addr,
	phys_bytes *phys_addr, size_t bytes)
{
	/* Look up the physical address corresponding to linear virtual address
	 * 'vir_addr' for process 'proc'. Return the size of the range covered
	 * by contiguous physical memory starting from that address; this may
	 * be anywhere between 0 and 'bytes' inclusive. If the return value is
	 * nonzero, and 'phys_addr' is non-NULL, 'phys_addr' will be set to the
	 * base physical address of the range. 'vir_addr' and 'bytes' need not
	 * be page-aligned, but the caller must have verified that the given
	 * linear range is valid for the given process at all.
	 */
	phys_bytes phys, next_phys;
	size_t len;

	assert(proc);
	assert(bytes > 0);
	assert(HASPT(proc));

	/* Look up the first page. */
	if (vm_lookup(proc, vir_addr, &phys, NULL) != OK)
		return 0;

	if (phys_addr != NULL)
		*phys_addr = phys;

	len = I386_PAGE_SIZE - (vir_addr % I386_PAGE_SIZE);
	vir_addr += len;
	next_phys = phys + len;

	/* Look up any next pages and test physical contiguity. */
	while (len < bytes) {
		if (vm_lookup(proc, vir_addr, &phys, NULL) != OK)
			break;

		if (next_phys != phys)
			break;

		len += I386_PAGE_SIZE;
		vir_addr += I386_PAGE_SIZE;
		next_phys += I386_PAGE_SIZE;
	}

	/* We might now have overshot the requested length somewhat. */
	return MIN(bytes, len);
}

/*===========================================================================*
 *				vm_check_range				     *
 *===========================================================================*/
int vm_check_range(struct proc *caller, struct proc *target,
	vir_bytes vir_addr, size_t bytes, int writeflag)
{
	/* Public interface to vm_suspend(), for use by kernel calls. On behalf
	 * of 'caller', call into VM to check linear virtual address range of
	 * process 'target', starting at 'vir_addr', for 'bytes' bytes. This
	 * function assumes that it will called twice if VM returned an error
	 * the first time (since nothing has changed in that case), and will
	 * then return the error code resulting from the first call. Upon the
	 * first call, a non-success error code is returned as well.
	 */
	int r;

	if ((caller->p_misc_flags & MF_KCALL_RESUME) &&
			(r = caller->p_vmrequest.vmresult) != OK)
		return r;

	vm_suspend(caller, target, vir_addr, bytes, VMSTYPE_KERNELCALL,
		writeflag);

	return VMSUSPEND;
}

/*===========================================================================*
 *                                 vmmemset                                  *
 *===========================================================================*/
int vm_memset(struct proc* caller, endpoint_t who, phys_bytes ph, int c,
	phys_bytes count)
{
	struct proc *whoptr = NULL;
	phys_bytes cur_ph = ph;
	phys_bytes left = count;
	int r = OK;

	if ((r = check_resumed_caller(caller)) != OK)
		return r;

	/* NONE for physical, otherwise virtual */
	if (who != NONE && !(whoptr = endpoint_lookup(who)))
		return ESRCH;

	c &= 0xFF;

	while (left > 0) {
		vir_bytes chunk = left;
		char *ptr;

		if (copy_resolve(whoptr, cur_ph, 1, &chunk, &ptr) !=
			WALK_OK) {
			/* If a process pagefaults, VM may help out */
			if (whoptr) {
				vm_suspend(caller, whoptr, ph, count,
						   VMSTYPE_KERNELCALL, 1);
				return VMSUSPEND;
			}

			/* Bad physical range ?! */
			panic("vm_memset: addr=%lx len=%lu\n", cur_ph, chunk);
		}

		memset(ptr, c, chunk);

		cur_ph += chunk;
		left -= chunk;
	}

	return OK;
}

/*===========================================================================*
 *				virtual_copy_f				     *
 *===========================================================================*/
int virtual_copy_f(
  struct proc * caller,
  struct vir_addr *src_addr,	/* source virtual address */
  struct vir_addr *dst_addr,	/* destination virtual address */
  vir_bytes bytes,		/* # of bytes to copy  */
  int vmcheck			/* if nonzero, can return VMSUSPEND */
)
{
/* Copy bytes from virtual address src_addr to virtual address dst_addr. */
  struct vir_addr *vir_addr[2];	/* virtual source and destination address */
  int i, r;
  struct proc *procs[2];

  assert((vmcheck && caller) || (!vmcheck && !caller));

  /* Check copy count. */
  if (bytes <= 0) return(EDOM);

  /* Do some more checks and map virtual addresses to physical addresses. */
  vir_addr[_SRC_] = src_addr;
  vir_addr[_DST_] = dst_addr;

  for (i=_SRC_; i<=_DST_; i++) {
  	endpoint_t proc_e = vir_addr[i]->proc_nr_e;
	int proc_nr;
	struct proc *p;

	if(proc_e == NONE) {
		p = NULL;
	} else {
		if(!isokendpt(proc_e, &proc_nr)) {
			printf("virtual_copy: no reasonable endpoint\n");
			return ESRCH;
		}
		p = proc_addr(proc_nr);
	}

	procs[i] = p;
  }

  if ((r = check_resumed_caller(caller)) != OK)
	return r;

  if((r=lin_lin_copy(procs[_SRC_], vir_addr[_SRC_]->offset,
  	procs[_DST_], vir_addr[_DST_]->offset, bytes)) != OK) {
	int writeflag;
  	struct proc *target = NULL;
  	phys_bytes lin;
  	if(r != EFAULT_SRC && r != EFAULT_DST)
  		panic("lin_lin_copy failed: %d",  r);
  	if(!vmcheck || !caller) {
    		return r;
  	}

  	if(r == EFAULT_SRC) {
  		lin = vir_addr[_SRC_]->offset;
  		target = procs[_SRC_];
		writeflag = 0;
  	} else if(r == EFAULT_DST) {
  		lin = vir_addr[_DST_]->offset;
  		target = procs[_DST_];
		writeflag = 1;
  	} else {
  		panic("r strange: %d",  r);
  	}

	assert(caller);
	assert(target);

	vm_suspend(caller, target, lin, bytes, VMSTYPE_KERNELCALL, writeflag);
	return VMSUSPEND;
  }

  return OK;
}

/*===========================================================================*
 *				data_copy				     *
 *===========================================================================*/
int data_copy(const endpoint_t from_proc, const vir_bytes from_addr,
	const endpoint_t to_proc, const vir_bytes to_addr,
	size_t bytes)
{
  struct vir_addr src, dst;

  src.offset = from_addr;
  dst.offset = to_addr;
  src.proc_nr_e = from_proc;
  dst.proc_nr_e = to_proc;
  assert(src.proc_nr_e != NONE);
  assert(dst.proc_nr_e != NONE);

  return virtual_copy(&src, &dst, bytes);
}

/*===========================================================================*
 *				data_copy_vmcheck			     *
 *===========================================================================*/
int data_copy_vmcheck(struct proc * caller,
	const endpoint_t from_proc, const vir_bytes from_addr,
	const endpoint_t to_proc, const vir_bytes to_addr,
	size_t bytes)
{
  struct vir_addr src, dst;

  src.offset = from_addr;
  dst.offset = to_addr;
  src.proc_nr_e = from_proc;
  dst.proc_nr_e = to_proc;
  assert(src.proc_nr_e != NONE);
  assert(dst.proc_nr_e != NONE);

  return virtual_copy_vmcheck(caller, &src, &dst, bytes);
}

void memory_init(void)
{
	/* Nothing to set up: the direct map is part of the bootstrap page
	 * table.
	 */
}

/*===========================================================================*
 *				arch_proc_init				     *
 *===========================================================================*/
void arch_proc_init(struct proc *pr, const reg_t ip, const reg_t sp,
	const reg_t ps_str, char *name)
{
	arch_proc_reset(pr);
	strlcpy(pr->p_name, name, sizeof(pr->p_name));

	/* set custom state we know */
	pr->p_reg.pc = ip;
	pr->p_reg.sp = sp;
	pr->p_reg.bx = ps_str;
}

static int oxpcie_mapping_index = -1,
	lapic_mapping_index = -1,
	ioapic_first_index = -1,
	ioapic_last_index = -1,
	video_mem_mapping_index = -1,
	usermapped_glo_index = -1,
	usermapped_index = -1, first_um_idx = -1;

extern char *video_mem;

extern char usermapped_start, usermapped_end, usermapped_nonglo_start;

int arch_phys_map(const int index,
			phys_bytes *addr,
			phys_bytes *len,
			int *flags)
{
	static int first = 1;
	int freeidx = 0;
	static char *ser_var = NULL;
	vir_bytes glo_len = (vir_bytes) &usermapped_nonglo_start -
			(vir_bytes) &usermapped_start;

	if(first) {
		memset(&minix_kerninfo, 0, sizeof(minix_kerninfo));
		video_mem_mapping_index = freeidx++;
		if(glo_len > 0) {
			usermapped_glo_index = freeidx++;
		}
		
		usermapped_index = freeidx++;
		first_um_idx = usermapped_index;
		if(usermapped_glo_index != -1)
			first_um_idx = usermapped_glo_index;

#ifdef USE_APIC
		if(lapic_addr)
			lapic_mapping_index = freeidx++;
		if (ioapic_enabled) {
			ioapic_first_index = freeidx;
			assert(nioapics > 0);
			freeidx += nioapics;
			ioapic_last_index = freeidx-1;
		}
#endif

#ifdef CONFIG_OXPCIE
		if((ser_var = env_get("oxpcie"))) {
			if(ser_var[0] != '0' || ser_var[1] != 'x') {
				printf("oxpcie address in hex please\n");
			} else {
				printf("oxpcie address is %s\n", ser_var);
				oxpcie_mapping_index = freeidx++;
			}
		}
#endif

		first = 0;
	}

	if(index == usermapped_glo_index) {
		*addr = vir2phys(&usermapped_start);
		*len = glo_len;
		*flags = VMMF_USER | VMMF_GLO;
		return OK;
	}
	else if(index == usermapped_index) {
		*addr = vir2phys(&usermapped_nonglo_start);
		*len = (vir_bytes) &usermapped_end -
			(vir_bytes) &usermapped_nonglo_start;
		*flags = VMMF_USER;
		return OK;
	}
	else if (index == video_mem_mapping_index) {
		/* map video memory in so we can print panic messages */
		*addr = MULTIBOOT_VIDEO_BUFFER;
		*len = I386_PAGE_SIZE;
		*flags = VMMF_WRITE;
		return OK;
	}
#ifdef USE_APIC
	else if (index == lapic_mapping_index) {
		/* map the local APIC if enabled */
		if (!lapic_addr)
			return EINVAL;
		*addr = lapic_addr;
		*len = 4 << 10 /* 4kB */;
		*flags = VMMF_UNCACHED | VMMF_WRITE;
		return OK;
	}
	else if (ioapic_enabled && index >= ioapic_first_index && index <= ioapic_last_index) {
		int ioapic_idx = index - ioapic_first_index;
		*addr = io_apic[ioapic_idx].paddr;
		assert(*addr);
		*len = 4 << 10 /* 4kB */;
		*flags = VMMF_UNCACHED | VMMF_WRITE;
		printf("ioapic map: addr 0x%lx\n", *addr);
		return OK;
	}
#endif

#if CONFIG_OXPCIE
	if(index == oxpcie_mapping_index) {
		*addr = strtoul(ser_var+2, NULL, 16);
		*len = 0x4000;
		*flags = VMMF_UNCACHED | VMMF_WRITE;
		return OK;
	}
#endif

	return EINVAL;
}

int arch_phys_map_reply(const int index, const vir_bytes addr)
{
#ifdef USE_APIC
	/* if local APIC is enabled */
	if (index == lapic_mapping_index && lapic_addr) {
		lapic_addr_vaddr = addr;
		return OK;
	}
	else if (ioapic_enabled && index >= ioapic_first_index &&
		index <= ioapic_last_index) {
		int i = index - ioapic_first_index;
		io_apic[i].vaddr = addr;
		return OK;
	}
#endif

#if CONFIG_OXPCIE
	if (index == oxpcie_mapping_index) {
		oxpcie_set_vaddr((unsigned char *) addr);
		return OK;
	}
#endif
	if(index == first_um_idx) {
		extern struct minix_ipcvecs minix_ipcvecs_softint;
		assert(addr < X86_64_VM_USER_TOP);
		/* The user mapping is below the kernel; the difference
		 * wraps around, which is fine as it is only ever added.
		 */
		usermapped_offset = addr - (vir_bytes) &usermapped_start;
#define FIXEDPTR(ptr) (void *) ((vir_bytes)ptr + usermapped_offset)
#define FIXPTR(ptr) ptr = FIXEDPTR(ptr)
#define ASSIGN(minixstruct) minix_kerninfo.minixstruct = FIXEDPTR(&minixstruct)
		ASSIGN(kinfo);
		ASSIGN(machine);
		ASSIGN(kmessages);
		ASSIGN(loadinfo);
		ASSIGN(kuserinfo);
		ASSIGN(arm_frclock); /* eh, why not. */
		ASSIGN(kclockinfo);

		/* the IPC routines to map into processes */
		DEBUGBASIC(("kernel: selecting fallback (int) ipc style\n"));
		minix_kerninfo.minix_ipcvecs = &minix_ipcvecs_softint;

		/* adjust the pointers of the functions and the struct
		 * itself to the user-accessible mapping
		 */
		FIXPTR(minix_kerninfo.minix_ipcvecs->send);
		FIXPTR(minix_kerninfo.minix_ipcvecs->receive);
		FIXPTR(minix_kerninfo.minix_ipcvecs->sendrec);
		FIXPTR(minix_kerninfo.minix_ipcvecs->senda);
		FIXPTR(minix_kerninfo.minix_ipcvecs->sendnb);
		FIXPTR(minix_kerninfo.minix_ipcvecs->notify);
		FIXPTR(minix_kerninfo.minix_ipcvecs->do_kernel_call);
		FIXPTR(minix_kerninfo.minix_ipcvecs);

		minix_kerninfo.kerninfo_magic = KERNINFO_MAGIC;
		minix_kerninfo.minix_feature_flags = minix_feature_flags;
		minix_kerninfo_user = (vir_bytes) FIXEDPTR(&minix_kerninfo);

		/* if libc_ipc is set, disable usermapped ipc functions
		 * and force binaries to use in-libc fallbacks.
		 */
		if(env_get("libc_ipc")) {
			printf("kernel: forcing in-libc fallback ipc style\n");
			minix_kerninfo.minix_ipcvecs = NULL;
		} else {
			minix_kerninfo.ki_flags |= MINIX_KIF_IPCVECS;
		}

		minix_kerninfo.ki_flags |= MINIX_KIF_USERINFO;

		return OK;
	}

	if(index == usermapped_index) return OK;

	if (index == video_mem_mapping_index) {
		video_mem_vaddr =  addr;
		return OK;
	}

	return EINVAL;
}

int arch_enable_paging(struct proc * caller)
{
	assert(caller->p_seg.p_cr3);

	/* load caller's page table */
	switch_address_space(caller);

	/* video memory and such stay reachable through the direct map */

	return OK;
}

void release_address_space(struct proc *pr)
{
	pr->p_seg.p_cr3_v = NULL;
}

/* computes a checksum of a buffer of a given length. The byte sum must be zero */
int platform_tbl_checksum_ok(void *ptr, unsigned int length)
{
	u8_t total = 0;
	unsigned int i;
	for (i = 0; i < length; i++)
		total += ((unsigned char *)ptr)[i];
	return !total;
}

int platform_tbl_ptr(phys_bytes start,
					phys_bytes end,
					unsigned increment,
					void * buff,
					unsigned size,
					phys_bytes * phys_addr,
					int ((* cmp_f)(void *)))
{
	phys_bytes addr;

	for (addr = start; addr < end; addr += increment) {
		memcpy(buff, PHYS_TO_DMAP(addr), size);
		if (cmp_f(buff)) {
			if (phys_addr)
				*phys_addr = addr;
			return 1;
		}
	}
	return 0;
}
