#!/usr/bin/env python3
"""mini-libc test suite.

Lays out tests by subsystem:

    core/      pure computation — string, ctype, stdlib, inttypes,
               setjmp, assert
    stdio/     stdio.h console + FILE* streams
    fs/        fs.h FAT32 fd API
    os/        syscall-backed services — errno, time, unistd
    negative/  tests that must fail (assert -> abort)

Each test prints `ok <check>` / `FAIL <check>` lines and ends with
`DONE-<tag> fails=N`. This runner compiles every test against
build/libmyc.a, injects the ELFs (plus tdata.txt) into a FAT32 image,
boots QEMU once, and `run`s each test checking serial output.

Usage: python3 mini-libc/test/run_tests.py [--qemu QEMU_BIN] [--keep]
Requires: make libmyc (run first), gcc -m32, mkfs.fat, qemu-system-i386.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTDIR = ROOT / "mini-libc" / "test"
LIBMYC_A = ROOT / "build" / "libmyc.a"
KERNEL = ROOT / "myos.bin"

sys.path.insert(0, str(ROOT / "test" / "integration"))
from fat32_image import add_root_file          # noqa: E402
from qemu_session import QemuSession           # noqa: E402
from common import IntegrationFailure          # noqa: E402

# (category, elf basename, source path relative to test/, done tag)
TESTS = [
    ("core",    "string.elf",   "core/string.c",   "DONE-string"),
    ("core",    "ctype.elf",    "core/ctype.c",    "DONE-ctype"),
    ("core",    "stdlib.elf",   "core/stdlib.c",   "DONE-stdlib"),
    ("core",    "inttypes.elf", "core/inttypes.c", "DONE-inttypes"),
    ("core",    "setjmp.elf",   "core/setjmp.c",   "DONE-setjmp"),
    ("core",    "assert.elf",   "core/assert.c",   "DONE-assert"),
    ("stdio",   "stdio.elf",    "stdio/stdio.c",   "DONE-stdio"),
    ("fs",      "fs.elf",       "fs/fs.c",         "DONE-fs"),
    ("os",      "errno.elf",    "os/errno.c",      "DONE-errno"),
    ("os",      "time.elf",     "os/time.c",       "DONE-time"),
    ("os",      "unistd.elf",   "os/unistd.c",     "DONE-unistd"),
    ("os",      "signal.elf",   "os/signal.c",     "DONE-signal"),
    ("os",      "process.elf", "os/process.c",   "DONE-process"),
]
# negative test: must abort; checked separately
AFail = ("negative", "afail.elf", "negative/afail.c")

CFLAGS = ["-m32", "-nostdlib", "-static", "-no-pie", "-ffreestanding",
          "-O2", "-I", str(ROOT / "mini-libc" / "include")]


def build_tests(workdir):
    elfs = []
    for cat, disk_name, src_rel, *_ in TESTS + [AFail]:
        src = TESTDIR / src_rel
        out = Path(workdir) / f"{cat}_{disk_name}"
        subprocess.run(
            ["gcc", *CFLAGS, str(src), str(LIBMYC_A), "-o", str(out)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        elfs.append((disk_name, out))
    return elfs


def create_disk(workdir, elfs):
    disk = Path(workdir) / "disk.img"
    with open(disk, "wb") as f:
        f.truncate(64 * 1024 * 1024)
    mkfs = shutil.which("mkfs.fat")
    if mkfs is None:
        raise IntegrationFailure("mkfs.fat is required")
    subprocess.run([mkfs, "-F", "32", "-n", "MYOS", str(disk)],
                   check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    for name, path in elfs:
        add_root_file(str(disk), name, path.read_bytes())
    # data file consumed by stdio/fs tests
    add_root_file(str(disk), "tdata.txt",
                  b"mini-libc-test-data\nsecond-line\n")
    # fs/fs.c destroys its fixture (rename+unlink); give it a dedicated
    # file so unistd/stdio keep a stable tdata.txt
    add_root_file(str(disk), "tfs.txt",
                  b"fs-fixture-data-123\n")
    return disk


def done_segment(text, marker):
    """Return the serial text after the LAST occurrence of marker."""
    idx = text.rfind(marker)
    return text[idx:] if idx >= 0 else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", default="qemu-system-i386")
    ap.add_argument("--kernel", default=str(KERNEL))
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--keep", action="store_true",
                    help="keep the temp workdir")
    args = ap.parse_args()

    if not LIBMYC_A.exists():
        subprocess.run(["make", "libmyc"], cwd=ROOT, check=True)
    if not Path(args.kernel).exists():
        subprocess.run(["make", "build"], cwd=ROOT, check=True)

    workdir = tempfile.mkdtemp(prefix="myos-libctest-")
    print(f"workdir: {workdir}")
    try:
        # FAT32 root dir on the 64 MiB test image holds ~16 entries;
        # keep <=7 ELFs per disk (plus tdata.txt) and boot one QEMU
        # session per chunk.
        chunks = [TESTS[i:i + 7] for i in range(0, len(TESTS), 6)]
        chunks.append([AFail + (None,)])   # negative test, own session
        results = []
        for chunk in chunks:
            elfs = build_tests_chunk(workdir, chunk)
            disk = create_disk(workdir, elfs)
            session = QemuSession(args.qemu, args.kernel, str(disk),
                                  timeout=args.timeout)
            try:
                session.start()
                session.wait_for(b"Unix timestamp", timeout=90)
                run_chunk(session, chunk, results)
            finally:
                session.close()

        bad = [r for r in results if not r[1]]
        print(f"\n{len(results) - len(bad)}/{len(results)} suites passed")
        if bad:
            for tag, _, msg in bad:
                print(f"  FAILED: {tag}: {msg}")
            return 1
        print("ALL PASS")
        return 0
    finally:
        if args.keep:
            print(f"kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


def build_tests_chunk(workdir, chunk):
    elfs = []
    for cat, disk_name, src_rel, *_ in chunk:
        src = TESTDIR / src_rel
        out = Path(workdir) / f"{cat}_{disk_name}"
        subprocess.run(
            ["gcc", *CFLAGS, str(src), str(LIBMYC_A), "-o", str(out)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        elfs.append((disk_name, out))
    return elfs


def run_chunk(session, chunk, results):
    for cat, disk_name, _src, marker in chunk:
        if marker is None:      # negative test
            tag = f"{cat}/{disk_name.split('.')[0]}"
            try:
                session.send_line(f"run {disk_name}")
                session.wait_for(b"Assertion failed", timeout=30)
                session.wait_for(b"afail-start", timeout=5)
                session.send_line("echo afail-shell-alive")
                session.wait_for(b"afail-shell-alive", timeout=15)
                seg = session.serial_text()[-3000:]
                ok = ("afail-unreachable" not in seg
                      and "afail-shell-alive" in seg)
                results.append((tag, ok, "assert->abort->shell alive"))
            except IntegrationFailure as e:
                results.append((tag, False, str(e)))
            print(("PASS" if results[-1][1] else "FAIL"),
                  tag, "-", results[-1][2])
            continue
        tag = f"{cat}/{disk_name.split('.')[0]}"
        before = len(session.serial_text())
        try:
            session.send_line(f"run {disk_name}")
            # wait for the complete DONE line, not just the tag prefix
            session.wait_for_regex(
                marker.encode() + rb" fails=\d+", timeout=40)
            seg = session.serial_text()[before:]
            tail = seg[seg.rfind(marker):]
            fails_line = [l for l in tail.splitlines() if "fails=" in l]
            fail_lines = [l.strip() for l in seg.splitlines()
                          if "FAIL" in l][:6]
            ok = (fails_line and "fails=0" in fails_line[-1]
                  and not fail_lines)
            msg = (fails_line[-1].strip() if fails_line
                   else "no DONE line")
            if fail_lines:
                msg += " | " + "; ".join(fail_lines)
            results.append((tag, ok, msg))
        except IntegrationFailure as e:
            results.append((tag, False, str(e)))
        print(("PASS" if results[-1][1] else "FAIL"),
              tag, "-", results[-1][2])


if __name__ == "__main__":
    sys.exit(main())
