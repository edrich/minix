# MINIX amd64 port

Status as of 2026-11-25: a complete amd64 (x86_64, LP64) system builds with
`build.sh`-style `release`, packs into a disk image, boots under QEMU (KVM or
TCG) from that disk to a multiuser login on serial, and passes the
minix-posix suite at the i386 baseline (details below).

## Design in brief

* Triple `x86_64-elf64-minix`, `MACHINE=amd64`, `MACHINE_ARCH=x86_64`; the
  in-tree clang 3.6 and binutils 2.23 have an x86_64 MINIX target.
* Messages are 128 bytes on LP64 (8-byte header, 120-byte payload) and 64
  bytes on ILP32; no message type was repacked
  (`minix/include/minix/ipcconst.h`).
* Kernel (`minix/kernel/arch/x86_64`): linked in the top 2GB and repackaged
  as ELF32 for Multiboot loaders; a 32-bit stub enters long mode.  All
  physical memory is direct-mapped in the kernel half, which replaces the
  i386 temporary mapping windows.  Traps use software interrupts only; PIC
  and PIT, single CPU.  Contexts that would return to a noncanonical address
  become SIGSEGV; `sigreturn` validates the context before using it.
* VM (`minix/servers/vm/arch/x86_64/pagetable_x86_64.c`): 4-level tables
  behind the existing `pt_*` interface, using a direct map of physical
  memory that the kernel gives VM.
* Userland is static by default as on i386; `ld.elf_so` works for the
  dynamically linked tests.  No TLS, as on i386.

## Test results (2026-11-27)

minix-posix suite on amd64 (QEMU/KVM, 256MB): **112 passed, 1 failed**
(`isofs`, which fails on i386 too): the same as the i386 baseline.
test64's out-of-memory messages and the VND diagnostics are expected, as on
i386.

test101 used to fail late in the suite: calloc() zeroed all of a large
allocation whenever it started below the heap's old high-water mark, and
the 64-bit system leaves less of the 256MB free.  calloc() now clears only
the reused part.

i386 with all of the branch's changes (2026-11-27): 112 passed, only
`isofs` failed (baseline).

## Build and test

All builds run in `docker/dev.sh`.  The object tree is `../obj.docker.amd64`.
Use that tree's `tooldir.*/bin/nbmake-amd64` (the tool directory name
contains the host kernel version: after a host kernel update build.sh would
build a new one, so name the existing one explicitly).

```sh
# first time: the cross toolchain
docker/dev.sh sh build.sh -m amd64 -O ../obj.docker.amd64 -U -u tools

# whole system + release sets + boot modules, then the disk image
T=$(realpath ../obj.docker.amd64/tooldir.*/bin)    # exactly one tooldir
docker/dev.sh $T/nbmake-amd64 -j12 build
docker/dev.sh $T/nbmake-amd64 -j12 release
docker/dev.sh env ARCH=amd64 OBJ=../obj.docker.amd64 CROSS_TOOLS=$T \
    CREATE_IMAGE_ONLY=1 ./releasetools/x86_hdimage.sh   # -> minix_amd64.img

# boot from the image, run commands
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --raw \
    --cmd 'uname -mp' --expect amd64

# full regression suite (about 25 minutes)
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --raw --timeout 3600 \
    --cmd 'cd /usr/tests/minix-posix && ./run'

# ramdisk-only boot smoke test (no disk), optionally with test programs
docker/dev.sh python3 releasetools/x86_boot_smoke.py --obj ../obj.docker.amd64 \
    --log .harness/amd64-smoke.log
```

There is no amd64 boot loader yet: the image is booted with QEMU's
`-kernel`/`-initrd` from `destdir.amd64/boot/minix/.temp` (the harness does
this).  `kernel` there is the ELF32 container; `minix/kernel/kernel.elf64`
in the object tree has the symbols.

## LP64 bug classes found so far

Most porting bugs fell into a few patterns, worth checking for in code that
has not been exercised on amd64 yet:

* Addresses or sizes in 32-bit message fields or `int`/`u32_t` variables
  (exec `ps_strings`, the VFS-to-VM file mapping request, ioctl requests
  with bit 31 set kept in an `int`).
* Pointers smuggled through a 32-bit `cp_grant_id_t` for requests from SELF
  (TTY kernel messages, libblockdriver partition reads in ahci, virtio_blk
  and vnd, libfsdriver's peek emulation).  Use the `iovec_t`/`ptr` view.
* `int` functions returning sentinels such as `NO_MEM`, which do not survive
  widening to a 64-bit type (VM's `findbit`).
* Hardware layouts declared with `phys_bytes` (at_wini PRD entries).
* `long` used to mean "32 bits" (`ex64lo`/`ex64hi`/`make64`, test66).
* NetBSD x86_64 assembly calling functions that are C on MINIX without an
  ABI-aligned stack (`setjmp` -> `sigprocmask`), and compiler-generated SSE
  accesses to device memory (VGA text memory under KVM).
* Stack and slab sizes tuned for 32-bit objects (VFS worker stacks, VM
  slab size range, VM's VFS request state).

## Remaining work

* Memory above 4GB: drivers with 32-bit DMA (at_wini's own buffers, 32-bit
  PCI BARs, `alloc_contig` has no "below 4GB" flag) and VM's bitmap are
  only sized/checked for the 64GB direct map; not tested with large RAM.
* No `syscall`/`sysret` fast path, APIC, ACPI, SMP, NX.
* No amd64 boot loader; the disk image is not bootable by itself.
* Network drivers, audio, video and other i386 drivers are not enabled for
  x86_64 yet; `minix-ports` has no x86_64 toolchain.
* The amd64 build makes format warnings non-fatal (`-Wno-error=format` in
  `share/mk/bsd.sys.mk`); most are long vs. long long mismatches, but the
  exception hides real ones too.
