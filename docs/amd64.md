# MINIX amd64 bring-up

Status as of 2026-11-14: the long-mode kernel, VM, core servers and static
ramdisk programs boot through `/etc/rc` under QEMU TCG. This is not yet a
complete installed amd64 system.

## Review and fixes

The review covered the recent amd64 commits and pending changes, focusing on
the boot path, IPC pointer widths, VM mappings, and ramdisk service startup.

* The first recorded boot failed at VFS root initialization because the memory
  driver contained an empty ramdisk. Newly enabled driver directories had not
  had their object directories created. Their binaries were built in the
  source tree, while the ramdisk Makefile expected them in the object tree.
  Run `nbmake-amd64 obj` in newly enabled directories before `dependall`.
* PM/VFS sent `ps_strings` through `m7_i5`, truncating the amd64 stack address
  to 32 bits. `/bin/sh` faulted at `0xfffffffffffffff0` in its startup code.
  Both directions now use the full-width `m7_v1` field, taken from message
  padding. Existing fields keep their offsets and total message size stays
  unchanged. **PM and VFS must be rebuilt and deployed together on all
  architectures.** The libc exec interface is unchanged.
* ProcFS now exposes `/proc/pci` and `/proc/cpuinfo` on amd64. Missing PCI
  enumeration previously prevented the ramdisk script from discovering a
  virtio root device.
* Ramdisk-only startup skips fsck of the already mounted root. Previously it
  attempted to open the nonexistent relative path `imgrd`.
* PM's legacy uname table now includes the amd64 architecture entry. Without
  it, every subsequent field was at the wrong index.

The existing pending LP64 fixes, driver build selections and ramdisk additions
were retained. The untracked driver binaries in the source tree are build
artifacts, not files to commit.

## Build and test

Build inside `docker/dev.sh`. The existing object trees are
`../obj.docker.amd64` and `../obj.docker.i386`. Use the matching
`tooldir.*/bin/nbmake-amd64` or `nbmake-i386` wrapper; if several tool directories
exist, select the current one explicitly.

After installing headers (`-C minix/include includes`), build the affected
components with `obj` followed by `dependall`. For a new object tree, first
build the toolchain, headers and libraries using `build.sh`; the smoke runner
does not build dependencies. The ramdisk Makefile's `PROGRAMS` and `dir.*`
variables list its prerequisites. Build those before the ramdisk, then build
the memory driver to embed the new image. Rebuild PM and VFS together.

The boot modules used by the smoke runner are listed in
`releasetools/x86_boot_smoke.py`. For amd64, `kernel` is the ELF32 container
required by the multiboot loader; `kernel.elf64` retains the long-mode image
and symbols. The user-space modules must all be ELF64.

```sh
docker/dev.sh python3 releasetools/x86_boot_smoke.py \
    --obj ../obj.docker.amd64 --log .harness/amd64-smoke.log
docker/dev.sh python3 releasetools/x86_boot_smoke.py \
    --obj ../obj.docker.amd64 --memory 5120 --timeout 90 \
    --log .harness/amd64-smoke-5g.log
docker/dev.sh python3 releasetools/x86_boot_smoke.py \
    --arch i386 --obj ../obj.docker.i386 --log .harness/i386-smoke.log
```

All three configurations passed after the exec and ramdisk fixes. The runner
requires VFS startup, the ramdisk root announcement, and the final ProcFS
mount; guest panics, segmentation faults and startup errors fail the gate.
It attaches no disks and terminates its QEMU process on success or failure.
Booting with 5 GiB is not a comprehensive high-memory stress test.

The minimal boot ramdisk has no login environment or utmpx database. The
`init: can't add utmpx record` warning after startup is expected for this
image. Do not reserve COM1 with `cttyline=0` while also using `console=tty00`;
TTY then refuses to initialize that serial line.

## Remaining work

* Build and package a complete amd64 root filesystem, boot it with the new
  kernel and servers, and run the MINIX regression suite. The existing
  `minix_x86.img` is an i386 image and was not replaced.
* Audit and test malformed signal/ptrace contexts and return-to-user faults.
  `restore_user_context_int` uses `iretq`; the exception handler has recovery
  for user-copy and FPU faults but no equivalent recovery around that return.
  Noncanonical saved instruction/stack pointers are a specific concern from
  static review, not covered by the boot gate.
* Exercise signal/FPU state preservation, copy-on-write, mmap, high-address
  grants, disk I/O and dynamic linking with dedicated guest tests.
* Resolve format warnings and remove the broad amd64 `-Wno-error=format`
  exception. It currently allows warnings beyond the known long/long-long
  formatting mismatches.
