# MINIX amd64 port

Status as of 2026-11-27: a complete amd64 (x86_64, LP64) system builds with
`build.sh`-style `release` into a disk image that boots by itself (the i386
boot loader loads the amd64 kernel), under QEMU (KVM or TCG) to a multiuser
login on the console or serial.  It passes the minix-posix suite at the i386
baseline, networks with the drivers for QEMU's emulated cards, and runs
with more than 4GB of memory (details below).

## Design in brief

* Triple `x86_64-elf64-minix`, `MACHINE=amd64`, `MACHINE_ARCH=x86_64`; the
  in-tree clang 3.6 and binutils 2.23 have an x86_64 MINIX target.  With
  `-m32` they build i386 code (the boot code).
* Messages are 128 bytes on LP64 (8-byte header, 120-byte payload) and 64
  bytes on ILP32; no message type was repacked
  (`minix/include/minix/ipcconst.h`).
* Kernel (`minix/kernel/arch/x86_64`): linked in the top 2GB and repackaged
  as ELF32 for Multiboot loaders; a 32-bit stub enters long mode.  All
  physical memory is direct-mapped in the kernel half, which replaces the
  i386 temporary mapping windows.  Traps use software interrupts only; PIC
  and PIT, single CPU.  Contexts that would return to a noncanonical address
  become SIGSEGV; `sigreturn` validates the context before using it.
* Boot: the 32-bit boot code (`sys/arch/i386/stand`: mbr, bootxx,
  boot_monitor) is built for amd64 too and loads the ELF32 kernel container
  and the modules as on i386.  On x86_64 it links a minimal i386 libkern
  (`sys/lib/libkern`) instead of the system's libminc.
* VM (`minix/servers/vm/arch/x86_64/pagetable_x86_64.c`): 4-level tables
  behind the existing `pt_*` interface, using a direct map of physical
  memory that the kernel gives VM.  Physically contiguous memory
  (`alloc_contig`, for device DMA) is allocated below 4GB.
* Userland is static by default as on i386; `ld.elf_so` works for the
  dynamically linked tests.  No TLS, as on i386.

## Test results (2026-11-27)

minix-posix suite on amd64 (QEMU/KVM, 256MB, booted from the disk image):
**112 passed, 1 failed** (`isofs`, which fails on i386 too): the same as
the i386 baseline.  test64's out-of-memory messages and the VND diagnostics
are expected, as on i386.  i386 with all of the branch's changes: 112
passed, only `isofs` failed (baseline).

Networking (DHCP, ping, an HTTP download from the host) works with QEMU's
e1000, virtio-net-pci, rtl8139, pcnet and ne2k_pci cards.

With 6GB of memory (about 2GB of it above 4GB): all of it is used; network
DMA, disk I/O (files read back match the build) and the memory tests 64,
74 and 87 work.

Earlier failures, for reference: test101 failed late in the suite because
calloc() zeroed all of a large allocation that started below the heap's old
high-water mark (it now clears only the reused part); test94 failed about
one run in three because the test aligned its BPF packet size arithmetic
differently from the capture code, which only matters on LP64.

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

# the image boots by itself
qemu-system-x86_64 -enable-kvm -m 256 -hda minix_amd64.img

# harness: boot (via -kernel, or --diskboot through the image's own boot
# loader), log in over serial, run commands
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --raw \
    --cmd 'uname -mp' --expect amd64
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --diskboot --raw \
    --cmd 'uname -mp' --expect amd64

# full regression suite (about 25 minutes)
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --diskboot --raw \
    --timeout 3600 --cmd 'cd /usr/tests/minix-posix && ./run'

# networking (--nic: e1000, virtio-net-pci, rtl8139, pcnet, ne2k_pci),
# memory above 4GB (--mem, in MB)
docker/dev.sh ./releasetools/x86_qemu.py --arch amd64 --net --nic rtl8139 \
    --mem 6144 --raw --cmd 'dhcpcd -qM -4 rl0; ping -c 3 10.0.2.2'

# ramdisk-only boot smoke test (no disk), optionally with test programs
docker/dev.sh python3 releasetools/x86_boot_smoke.py --obj ../obj.docker.amd64 \
    --log .harness/amd64-smoke.log
```

`--diskboot` uses SeaBIOS's serial console (fw_cfg `etc/sercon-port`) to
drive the boot monitor: it picks "Drop to boot prompt" and types the
`load_mods`/`multiboot` commands with its kernel arguments.  Without it the
harness loads `destdir.amd64/boot/minix/.temp` with QEMU's `-kernel`/
`-initrd`.  `kernel` there is the ELF32 container; `minix/kernel/kernel.elf64`
in the object tree has the symbols.

To audit code for LP64 truncations, build with `MINIX_LP64_AUDIT=yes`
(`-Wshorten-64-to-32` as a warning) and read the warnings.

## LP64 bug classes found so far

Most porting bugs fell into a few patterns, worth checking for in code that
has not been exercised on amd64 yet:

* Addresses or sizes in 32-bit message fields or `int`/`u32_t` variables
  (exec `ps_strings`, the VFS-to-VM file mapping request and its length,
  ioctl requests with bit 31 set kept in an `int`).
* Pointers smuggled through a 32-bit `cp_grant_id_t` for requests from SELF
  (TTY kernel messages, libblockdriver partition reads in ahci, virtio_blk,
  vnd and floppy, libfsdriver's peek emulation).  Use the `iovec_t`/`ptr`
  view, or keep the full `iov_addr`.
* Pointers through 32-bit typedefs (lance's `Address`).
* `int` functions returning sentinels such as `NO_MEM`, which do not survive
  widening to a 64-bit type (VM's `findbit`).
* Hardware layouts declared with `phys_bytes` or `long` (at_wini PRD
  entries, lance's init block); device registers written with the low half
  of a 64-bit physical address only (ahci).
* `long` used to mean "32 bits" (`ex64lo`/`ex64hi`/`make64`, test66), and
  APIs that pass `long` values through `int` (ptrace).
* NetBSD x86_64 assembly calling functions that are C on MINIX without an
  ABI-aligned stack (`setjmp` -> `sigprocmask`), and compiler-generated SSE
  accesses to device memory (VGA text memory under KVM).
* Stack and slab sizes tuned for 32-bit objects (VFS worker stacks, VM
  slab size range, VM's VFS request state).
* Build dependencies: a Makefile listing objects in `SRCS` (trace) gets no
  header dependencies, so header changes leave stale objects.

## Remaining work

* No `syscall`/`sysret` fast path, APIC, ACPI, SMP, NX.
* Memory above 4GB: drivers that DMA from static buffers obtained with
  `sys_umap` rather than `alloc_contig` (dec21140A, fxp, audio) are not
  built for amd64 and would need care; ATAPI transfers to buffers above
  4GB use PIO.  Only tested up to 6GB.
* Drivers not built for amd64: the other network drivers (3c90x, atl2,
  dec21140A, dpeth, fxp, ip1000, rtl8169, vt6105; fxp does not work with
  QEMU's 8255x models on i386 either), audio, and other i386-only drivers.
* trace(1) prints no stack traces (`-s`) on amd64: `kernel.c` only knows
  the i386 frame layout.
* During test73 (VM secondary cache test) at_wini logs one "timeout on
  command 0xc8" (READ DMA) on amd64 in every run, never on i386; the retry
  succeeds and the test passes.
* `minix-ports` has no x86_64 toolchain, so the ports (and the desktop
  stack) are i386-only.
