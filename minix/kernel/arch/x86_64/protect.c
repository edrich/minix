/* This file contains code for initialization of protected mode, to initialize
 * code and data segment descriptors, and to initialize global descriptors
 * for local descriptors in the process table.
 */

#include <assert.h>
#include <string.h>

#include <minix/cpufeature.h>
#include <sys/types.h>
#include "kernel/kernel.h"

#include "arch_proto.h"

#include <sys/exec.h>
#include <libexec.h>

#define INT_GATE_TYPE	(INT_286_GATE | DESC_386_BIT)
#define TSS_TYPE	(AVL_286_TSS  | DESC_386_BIT)

/* Video memory, through the direct map. */
char *video_mem = (char *) (KERNEL_DMAP_BASE + MULTIBOOT_VIDEO_BUFFER);

/* Storage for gdt, idt and tss. */
struct segdesc_s gdt[GDT_SIZE] __aligned(DESC_SIZE);
struct gatedesc_s idt[IDT_SIZE] __aligned(DESC_SIZE);
struct tss_s tss[CONFIG_MAX_CPUS];

reg_t k_percpu_stacks[CONFIG_MAX_CPUS];

int prot_init_done = 0;

phys_bytes vir2phys(void *vir)
{
	extern char _kern_vir_base, _kern_phys_base;	/* in kernel.lds */
	vir_bytes offset = (vir_bytes) &_kern_vir_base -
		(vir_bytes) &_kern_phys_base;
	return (phys_bytes)vir - offset;
}

/*===========================================================================*
 *				enable_iop				     * 
 *===========================================================================*/
void enable_iop(struct proc *pp)
{
/* Allow a user process to use I/O instructions.  Change the I/O Permission
 * Level bits in the psw. These specify least-privileged Current Permission
 * Level allowed to execute I/O instructions. Users and servers have CPL 3. 
 * You can't have less privilege than that. Kernel has CPL 0, tasks CPL 1.
 */
  pp->p_reg.psw |= 0x3000;
}


/*===========================================================================*
 *				init_seg				     *
 *===========================================================================*/
static void init_seg(int index, int privilege, int executable)
{
/* Build descriptor for a code or data segment. In long mode the base and
 * limit of such segments are ignored; what matters is the type, the
 * privilege level and, for code, the L (64-bit) flag.
 */
  struct segdesc_s *segdp = &gdt[index];

  memset(segdp, 0, sizeof(*segdp));
  segdp->limit_low = 0xFFFF;
  if (executable) {
	segdp->access = (privilege << DPL_SHIFT)
	        | (PRESENT | SEGMENT | EXECUTABLE | READABLE);
	segdp->granularity = GRANULAR | LONGMODE | LIMIT_HIGH;
  } else {
	segdp->access = (privilege << DPL_SHIFT)
		| (PRESENT | SEGMENT | WRITEABLE | ACCESSED);
	segdp->granularity = GRANULAR | BIG | LIMIT_HIGH;
  }
}

static struct gate_table_s gate_table_pic[] = {
	{ hwint00, VECTOR( 0), INTR_PRIVILEGE },
	{ hwint01, VECTOR( 1), INTR_PRIVILEGE },
	{ hwint02, VECTOR( 2), INTR_PRIVILEGE },
	{ hwint03, VECTOR( 3), INTR_PRIVILEGE },
	{ hwint04, VECTOR( 4), INTR_PRIVILEGE },
	{ hwint05, VECTOR( 5), INTR_PRIVILEGE },
	{ hwint06, VECTOR( 6), INTR_PRIVILEGE },
	{ hwint07, VECTOR( 7), INTR_PRIVILEGE },
	{ hwint08, VECTOR( 8), INTR_PRIVILEGE },
	{ hwint09, VECTOR( 9), INTR_PRIVILEGE },
	{ hwint10, VECTOR(10), INTR_PRIVILEGE },
	{ hwint11, VECTOR(11), INTR_PRIVILEGE },
	{ hwint12, VECTOR(12), INTR_PRIVILEGE },
	{ hwint13, VECTOR(13), INTR_PRIVILEGE },
	{ hwint14, VECTOR(14), INTR_PRIVILEGE },
	{ hwint15, VECTOR(15), INTR_PRIVILEGE },
	{ NULL, 0, 0}
};

static struct gate_table_s gate_table_exceptions[] = {
	{ divide_error, DIVIDE_VECTOR, INTR_PRIVILEGE },
	{ single_step_exception, DEBUG_VECTOR, INTR_PRIVILEGE },
	{ nmi, NMI_VECTOR, INTR_PRIVILEGE },
	{ breakpoint_exception, BREAKPOINT_VECTOR, USER_PRIVILEGE },
	{ overflow, OVERFLOW_VECTOR, USER_PRIVILEGE },
	{ bounds_check, BOUNDS_VECTOR, INTR_PRIVILEGE },
	{ inval_opcode, INVAL_OP_VECTOR, INTR_PRIVILEGE },
	{ copr_not_available, COPROC_NOT_VECTOR, INTR_PRIVILEGE },
	{ double_fault, DOUBLE_FAULT_VECTOR, INTR_PRIVILEGE },
	{ copr_seg_overrun, COPROC_SEG_VECTOR, INTR_PRIVILEGE },
	{ inval_tss, INVAL_TSS_VECTOR, INTR_PRIVILEGE },
	{ segment_not_present, SEG_NOT_VECTOR, INTR_PRIVILEGE },
	{ stack_exception, STACK_FAULT_VECTOR, INTR_PRIVILEGE },
	{ general_protection, PROTECTION_VECTOR, INTR_PRIVILEGE },
	{ page_fault, PAGE_FAULT_VECTOR, INTR_PRIVILEGE },
	{ copr_error, COPROC_ERR_VECTOR, INTR_PRIVILEGE },
	{ alignment_check, ALIGNMENT_CHECK_VECTOR, INTR_PRIVILEGE },
	{ machine_check, MACHINE_CHECK_VECTOR, INTR_PRIVILEGE },
	{ simd_exception, SIMD_EXCEPTION_VECTOR, INTR_PRIVILEGE },
	{ ipc_entry_softint_orig, IPC_VECTOR_ORIG, USER_PRIVILEGE },
	{ kernel_call_entry_orig, KERN_CALL_VECTOR_ORIG, USER_PRIVILEGE },
	{ ipc_entry_softint_um, IPC_VECTOR_UM, USER_PRIVILEGE },
	{ kernel_call_entry_um, KERN_CALL_VECTOR_UM, USER_PRIVILEGE },
	{ NULL, 0, 0}
};

int tss_init(unsigned cpu, void * kernel_stack)
{
	extern char k_df_stktop;
	struct tss_s * t = &tss[cpu];
	int index = TSS_INDEX(cpu);
	struct sysdesc_s *tssgdt;
	vir_bytes base = (vir_bytes) t;
	vir_bytes limit = sizeof(struct tss_s) - 1;

	/* A TSS descriptor is twice the size of a segment descriptor. */
	tssgdt = (struct sysdesc_s *) &gdt[index];
	memset(tssgdt, 0, sizeof(*tssgdt));
	tssgdt->low.limit_low = limit;
	tssgdt->low.base_low = base;
	tssgdt->low.base_middle = base >> BASE_MIDDLE_SHIFT;
	tssgdt->low.base_high = base >> BASE_HIGH_SHIFT;
	tssgdt->low.granularity = limit >> GRANULARITY_SHIFT;
	tssgdt->low.access = PRESENT | (INTR_PRIVILEGE << DPL_SHIFT) | TSS_TYPE;
	tssgdt->base_upper = base >> 32;

	/* Build TSS. */
	memset(t, 0, sizeof(*t));
	t->iobase = sizeof(struct tss_s);	/* empty i/o permissions map */

	/* 
	 * make space for process pointer and cpu id and point to the first
	 * usable word
	 */
	k_percpu_stacks[cpu] = t->sp0 = ((reg_t) kernel_stack) - X86_STACK_TOP_RESERVED;
	/* 
	 * set the cpu id at the top of the stack so we know on which cpu is
	 * this stack in use when we trap to kernel
	 */
	*((reg_t *)(t->sp0 + 1 * sizeof(reg_t))) = cpu;

	/* A double fault gets a stack of its own, as the one in use may
	 * well be what is broken.
	 */
	t->ist[IST_DOUBLE_FAULT - 1] = (reg_t) &k_df_stktop;

	return SEG_SELECTOR(index);
}

void int_gate(struct gatedesc_s *tab,
	unsigned vec_nr, vir_bytes offset, unsigned dpl_type)
{
/* Build descriptor for an interrupt gate. */
  register struct gatedesc_s *idp;

  idp = &tab[vec_nr];
  memset(idp, 0, sizeof(*idp));
  idp->offset_low = offset;
  idp->selector = KERN_CS_SELECTOR;
  idp->p_dpl_type = dpl_type;
  idp->offset_middle = offset >> OFFSET_HIGH_SHIFT;
  idp->offset_high = offset >> 32;
  if (vec_nr == DOUBLE_FAULT_VECTOR)
	idp->ist = IST_DOUBLE_FAULT;
}

void int_gate_idt(unsigned vec_nr, vir_bytes offset, unsigned dpl_type)
{
	int_gate(idt, vec_nr, offset, dpl_type);
}

void idt_copy_vectors(struct gate_table_s * first)
{
	struct gate_table_s *gtp;
	for (gtp = first; gtp->gate; gtp++) {
		int_gate(idt, gtp->vec_nr, (vir_bytes) gtp->gate,
				PRESENT | INT_GATE_TYPE |
				(gtp->privilege << DPL_SHIFT));
	}
}

void idt_copy_vectors_pic(void)
{
	idt_copy_vectors(gate_table_pic);
}

void idt_init(void)
{
	idt_copy_vectors_pic();
	idt_copy_vectors(gate_table_exceptions);
}

struct desctableptr_s gdt_desc, idt_desc;

void idt_reload(void)
{
	x86_lidt(&idt_desc);
}

multiboot_module_t *bootmod(int pnr)
{
	int i;

	assert(pnr >= 0);

	/* Search for desired process in boot process
	 * list. The first NR_TASKS ones do not correspond
	 * to a module, however, so we don't search those.
	 */
	for(i = NR_TASKS; i < NR_BOOT_PROCS; i++) {
		int p;
		p = i - NR_TASKS;
		if(image[i].proc_nr == pnr) {
			assert(p < MULTIBOOT_MAX_MODS);
			assert(p < kinfo.mbi.mi_mods_count);
			return &kinfo.module_list[p];
		}
	}

	panic("boot module %d not found", pnr);
}

int booting_cpu = 0;

void prot_load_selectors(void)
{
  /* this function is called by both prot_init by the BSP and
   * the early AP booting code in mpx.S by secondary CPU's.
   * everything is set up the same except for the TSS that is per-CPU.
   */
  x86_lgdt(&gdt_desc);	/* Load gdt */ 
  idt_init();
  idt_reload();
  x86_lldt(0); 			/* No ldt */
  x86_ltr(TSS_SELECTOR(booting_cpu));

  x86_load_kerncs();
  x86_load_ss(KERN_DS_SELECTOR);
  /* The data segment registers are not used for anything in long mode,
   * by the kernel nor by processes, and are not switched. Give them a
   * value that is valid for both.
   */
  x86_load_ds(USER_DS_SELECTOR);
  x86_load_es(USER_DS_SELECTOR);
  x86_load_fs(0);
  x86_load_gs(0);
}

/*===========================================================================*
 *				prot_init				     *
 *===========================================================================*/
void prot_init(void)
{
  extern char k_boot_stktop;

  /* Only the software interrupt trap style is implemented; see
   * usermapped_data_arch.c.
   */

  memset(gdt, 0, sizeof(gdt));
  memset(idt, 0, sizeof(idt));

  /* Build GDT, IDT, IDT descriptors. */
  gdt_desc.base = (vir_bytes) gdt;
  gdt_desc.limit = sizeof(gdt)-1;
  idt_desc.base = (vir_bytes) idt;
  idt_desc.limit = sizeof(idt)-1;
  tss_init(0, &k_boot_stktop);

  /* Build GDT */
  init_seg(KERN_CS_INDEX, INTR_PRIVILEGE, 1);
  init_seg(KERN_DS_INDEX, INTR_PRIVILEGE, 0);
  init_seg(USER_DS_INDEX, USER_PRIVILEGE, 0);
  init_seg(USER_CS_INDEX, USER_PRIVILEGE, 1);

  /* Currently the multiboot segments are loaded; which is fine, but
   * let's replace them with the ones from our own GDT so we test
   * right away whether they work as expected.
   */
  prot_load_selectors();

  /* Set up a new post-relocate bootstrap pagetable so that
   * we can map in VM, and we no longer rely on pre-relocated
   * data.
   */

  pg_clear();
  pg_noidentity(&kinfo); /* No 1:1 mapping: the direct map replaces it. */
  pg_mapkernel();
  pg_load();

  prot_init_done = 1;
}

static int alloc_for_vm = 0;

void arch_post_init(void)
{
  /* Let memory mapping code know what's going on at bootstrap time */
  struct proc *vm;
  vm = proc_addr(VM_PROC_NR);
  get_cpulocal_var(ptproc) = vm;
  pg_info(&vm->p_seg.p_cr3, &vm->p_seg.p_cr3_v);
}

/* The kernel does not link against libexec on this architecture (see
 * Makefile.inc); these two callbacks are all it needs from the general
 * part of that library.
 */
int libexec_copy_memcpy(struct exec_info *execi,
	off_t off, vir_bytes vaddr, size_t len)
{
	assert(off + len <= execi->hdr_len);
	memcpy((char *) vaddr, (char *) execi->hdr + off, len);
	return OK;
}

int libexec_clear_memset(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
	memset((char *) vaddr, 0, len);
	return OK;
}

static int libexec_pg_alloc(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
        pg_map(PG_ALLOCATEME, vaddr, vaddr+len, &kinfo);
  	pg_load();
        memset((char *) vaddr, 0, len);
	alloc_for_vm += len;
        return OK;
}

void arch_boot_proc(struct boot_image *ip, struct proc *rp)
{
	multiboot_module_t *mod;
	struct ps_strings *psp;
	char *sp;

	if(rp->p_nr < 0) return;

	mod = bootmod(rp->p_nr);

	/* Important special case: we put VM in the bootstrap pagetable
	 * so it can run.
	 */

	if(rp->p_nr == VM_PROC_NR) {
		struct exec_info execi;

		memset(&execi, 0, sizeof(execi));

		/* exec parameters */
		execi.stack_high = kinfo.user_sp;
		execi.stack_size = 64 * 1024;	/* not too crazy as it must be preallocated */
		execi.proc_e = ip->endpoint;
		execi.hdr = PHYS_TO_DMAP(mod->mod_start); /* phys mem direct */
		execi.filesize = execi.hdr_len = mod->mod_end - mod->mod_start;
		strlcpy(execi.progname, ip->proc_name, sizeof(execi.progname));
		execi.frame_len = 0;

		/* callbacks for use in the kernel */
		execi.copymem = libexec_copy_memcpy;
		execi.clearmem = libexec_clear_memset;
		execi.allocmem_prealloc_junk = libexec_pg_alloc;
		execi.allocmem_prealloc_cleared = libexec_pg_alloc;
		execi.allocmem_ondemand = libexec_pg_alloc;
		execi.clearproc = NULL;

		/* parse VM ELF binary and alloc/map it into bootstrap pagetable */
		if(libexec_load_elf(&execi) != OK)
			panic("VM loading failed");

		/* Setup a ps_strings struct on the stack, pointing to the
		 * following argv, envp. */
		sp = (char *)execi.stack_high;
		sp -= sizeof(struct ps_strings);
		psp = (struct ps_strings *) sp;

		/* Take the stack pointer down three words to give startup code
		 * something to use as "argc", "argv" and "envp".
		 */
		sp -= (sizeof(void *) + sizeof(void *) + sizeof(void *));

		// linear address space, so it is available.
		psp->ps_argvstr = (char **)(sp + sizeof(void *));
		psp->ps_nargvstr = 0;
		psp->ps_envstr = psp->ps_argvstr + sizeof(void *);
		psp->ps_nenvstr = 0;

		arch_proc_init(rp, execi.pc, (vir_bytes)sp,
			execi.stack_high - sizeof(struct ps_strings),
			ip->proc_name);

		/* Free VM blob that was just copied into existence. */
		add_memmap(&kinfo, mod->mod_start, mod->mod_end-mod->mod_start);
		mod->mod_end = mod->mod_start = 0;

		/* Remember them */
		kinfo.vm_allocated_bytes = alloc_for_vm;
	}
}
