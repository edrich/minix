#!/usr/bin/env python3
"""Boot built MINIX x86 modules through ramdisk /etc/rc, without a disk image.

Run inside docker/dev.sh after building the kernel, boot modules, ramdisk and
memory driver. This checks early service startup, not a full installed system.
"""

import argparse
import pathlib
import selectors
import subprocess
import tempfile
import time


MODULES = (
    "minix/servers/ds/ds",
    "minix/servers/rs/rs",
    "minix/servers/pm/pm",
    "minix/servers/sched/sched",
    "minix/servers/vfs/vfs",
    "minix/drivers/storage/memory/memory",
    "minix/drivers/tty/tty/tty",
    "minix/servers/mib/mib",
    "minix/servers/vm/vm",
    "minix/fs/pfs/pfs",
    "minix/fs/mfs/mfs",
    "sbin/init/init",
)


def test_ramdisk(obj, tests, nbmake, work, buildlog):
    """Embed tests in a separate memory driver, leaving the normal image alone."""
    source = obj / "minix/drivers/storage/ramdisk"
    ramdisk = work / "ramdisk"
    ramdisk.mkdir()
    memory = work / "memory"
    memory.mkdir()
    rc = ramdisk / "rc"
    rc.write_text((source / "rc").read_text() + "\n" + "\n".join(
        f"/boot-test-{i} || {{ echo 'BOOTTEST:FAIL:{i}'; exit 1; }}"
        for i in range(len(tests))) + "\necho 'BOOTTEST:PASS'\n")
    lines = []
    for line in (source / "proto.gen").read_text().splitlines():
        fields = line.split()
        if len(fields) == 5 and fields[1].startswith("-"):
            path = rc if fields[-1] == "rc" else source / fields[-1]
            fields[-1] = str(path)
            line = " ".join(fields)
        lines.append(line)
    if lines[-1] != "$":
        raise ValueError("ramdisk prototype lacks its final directory terminator")
    lines[-1:-1] = [f"boot-test-{i} ---755 0 0 {test}"
                   for i, test in enumerate(tests)]
    proto = ramdisk / "proto"
    proto.write_text("\n".join(lines) + "\n")
    tool = nbmake.parent / "nbmkfs.mfs"
    repo = pathlib.Path(__file__).resolve().parent.parent
    with buildlog.open("wb") as log:
        subprocess.run([str(tool), str(ramdisk / "image"), str(proto)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(nbmake), "-C", str(repo / "minix/drivers/storage/memory"),
                        f"MAKEOBJDIR={memory}", "dependall"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    return memory / "memory"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("amd64", "i386"), default="amd64")
    parser.add_argument("--obj", type=pathlib.Path, required=True)
    parser.add_argument("--log", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--memory", type=int, default=256, help="guest RAM in MiB")
    parser.add_argument("--test", type=pathlib.Path, action="append", default=[],
                        help="static guest test binary to run after startup (repeatable)")
    parser.add_argument("--nbmake", type=pathlib.Path,
                        help="matching nbmake wrapper, required with --test")
    args = parser.parse_args()
    if args.timeout <= 0 or args.memory <= 0:
        parser.error("timeout and memory must be positive")

    obj = args.obj.resolve()
    tests = [path.resolve() for path in args.test]
    if tests and not args.nbmake:
        parser.error("--test requires --nbmake for building the isolated memory driver")
    kernel = obj / "minix/kernel/kernel"
    modules = [obj / name for name in MODULES]
    elf_class = 2 if args.arch == "amd64" else 1
    # The amd64 kernel has an ELF32 container for QEMU's multiboot loader;
    # kernel.elf64 is the original long-mode image retained by the build.
    check_kernel = kernel.with_name("kernel.elf64") if elf_class == 2 else kernel
    for path in [check_kernel] + modules + tests:
        with path.open("rb") as binary:
            if binary.read(5) != b"\x7fELF" + bytes([elf_class]):
                parser.error(f"{path} is not an ELF binary for {args.arch}")

    args.log = args.log.resolve()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="boot-smoke-", dir=args.log.parent) as temp:
        if tests:
            # mkfs prototypes cannot quote whitespace in source paths.
            paths = [obj, pathlib.Path(temp)] + tests
            if any(any(c.isspace() for c in str(path)) for path in paths):
                parser.error("test ramdisk paths must not contain whitespace")
            modules[5] = test_ramdisk(obj, tests, args.nbmake.resolve(),
                                     pathlib.Path(temp),
                                     args.log.with_suffix(".build.log"))
        return boot(args, kernel, modules, bool(tests))


def boot(args, kernel, modules, testing):
    # Keep tty00 available to userland. The early kernel diagnostics use
    # COM1, but cttyline=0 would reserve it and prevent TTY from using it.
    command = [
        "qemu-system-x86_64" if args.arch == "amd64" else "qemu-system-i386",
        "-machine", "pc", "-accel", "tcg", "-m", str(args.memory),
        "-display", "none", "-no-reboot", "-monitor", "none",
        "-serial", "stdio", "-nic", "none",
        "-kernel", str(kernel), "-initrd", ",".join(map(str, modules)),
        "-append", "bootramdisk=1 console=tty00 cttyline=1 cttybaud=115200 verbose=3",
    ]
    expected = (b"Started VFS:", b"Root device name is imgrd",
                b"none is mounted on /proc")
    failures = (b"panic:", b"BOOTTEST:FAIL:", b"terminated abnormally",
                b"couldn't", b"grep: /proc/pci:", b"fatal")
    if testing:
        expected += (b"BOOTTEST:PASS",)
    else:
        failures += (b"SIGSEGV",)
    output = b""
    completed_at = None
    error = "boot timed out"
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("wb") as log:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    for key, _ in selector.select(timeout=0.1):
                        chunk = key.fileobj.read1(65536)
                        if chunk:
                            log.write(chunk)
                            log.flush()
                            output += chunk
                    if any(marker in output for marker in failures):
                        error = "guest reported a boot failure"
                        break
                    if process.poll() is not None:
                        error = f"QEMU exited early ({process.returncode})"
                        break
                    if all(marker in output for marker in expected):
                        if completed_at is None:
                            completed_at = time.monotonic()
                        if time.monotonic() - completed_at >= 1:
                            print(f"PASS: {args.arch} ramdisk startup ({args.log})")
                            return 0
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            process.stdout.close()
    print(f"FAIL: {error}; see {args.log}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
