#!/usr/bin/env python3
"""Boot the dedicated 6 GiB Multiboot image and exercise a real high PAE frame."""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from common import ROOT


PASS_PATTERN = re.compile(
    rb"\[HIGHMEM\] PASS: physical=(0x[0-9A-F]{8}):(0x[0-9A-F]{8})"
    rb" mapped, pattern verified, unmapped, and freed"
)


def fail(message, serial=b"", detail=""):
    print(f"✗ FAIL: high-memory QEMU test: {message}", file=sys.stderr)
    if serial:
        print("--- serial output ---", file=sys.stderr)
        print(serial.decode("ascii", errors="replace"), file=sys.stderr)
    if detail:
        print(detail, file=sys.stderr)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--timeout", type=float, default=75.0)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    qemu = shutil.which(args.qemu)
    if qemu is None:
        return fail(f"QEMU executable not found: {args.qemu}")
    kernel = Path(args.kernel).resolve()
    if not kernel.is_file():
        return fail(f"kernel image does not exist: {kernel}")

    process = None
    with tempfile.TemporaryDirectory(prefix="myos-highmem-") as temp:
        tempdir = Path(temp)
        iso_root = tempdir / "iso"
        grub_dir = iso_root / "boot" / "grub"
        grub_dir.mkdir(parents=True)
        shutil.copy2(kernel, iso_root / "boot" / kernel.name)
        (grub_dir / "grub.cfg").write_text(
            "set timeout=0\n"
            "set default=0\n"
            "menuentry \"MyOS high-memory test\" {\n"
            f"    multiboot /boot/{kernel.name}\n"
            "    boot\n"
            "}\n",
            encoding="ascii",
        )
        iso = tempdir / "highmem-test.iso"
        try:
            grub = subprocess.run(
                ["grub-mkrescue", "-o", str(iso), str(iso_root),
                 "--modules=multiboot biosdisk iso9660"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=45,
                check=False,
            )
        except (OSError, subprocess.SubprocessError) as error:
            return fail(f"could not create GRUB test ISO: {error}")
        if grub.returncode != 0:
            return fail("grub-mkrescue failed", detail=grub.stdout)

        serial_path = tempdir / "serial.log"
        command = [
            qemu, "-machine", "pc,accel=tcg", "-rtc", "base=utc",
            "-m", "6G",
            "-cdrom", str(iso), "-boot", "d", "-display", "none",
            "-no-reboot", "-no-shutdown", "-monitor", "none",
            "-serial", f"file:{serial_path}",
        ]
        deadline = time.monotonic() + args.timeout
        try:
            process = subprocess.Popen(
                command, cwd=ROOT, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            )
            while time.monotonic() < deadline:
                serial = (serial_path.read_bytes()
                          if serial_path.exists() else b"")
                if b"[HIGHMEM] FAIL:" in serial:
                    return fail("kernel reported a test failure", serial)
                match = PASS_PATTERN.search(serial)
                if match:
                    physical = int(match.group(1), 16) << 32
                    physical |= int(match.group(2), 16)
                    if physical < 0x100000000:
                        return fail("PASS address was below 4 GiB", serial)
                    line = match.group(0).decode("ascii")
                    print(line, flush=True)
                    print(
                        f"✓ PASS: QEMU allocated, mapped, tested, and freed "
                        f"physical page 0x{physical:016X} with -m 6G",
                        flush=True,
                    )
                    return 0
                if process.poll() is not None:
                    detail = process.stderr.read().decode(
                        "utf-8", errors="replace"
                    ) if process.stderr else ""
                    return fail(
                        f"QEMU exited early with status {process.returncode}",
                        serial, detail,
                    )
                time.sleep(0.1)
            serial = (serial_path.read_bytes()
                      if serial_path.exists() else b"")
            return fail(f"timed out after {args.timeout:g}s", serial)
        except OSError as error:
            return fail(f"could not start QEMU: {error}")
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)


if __name__ == "__main__":
    sys.exit(main())
