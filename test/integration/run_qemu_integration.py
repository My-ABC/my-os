#!/usr/bin/env python3
"""Run bounded headless QEMU integration checks for MyOS."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from common import IntegrationFailure, ROOT
from fat32_image import create_test_disk
from qemu_session import QemuSession
from page_fault_scenarios import (
    check_kernel_fault_panic,
    check_page_faults,
    check_user_oom,
)
from ring3_scenarios import check_ring3_and_builtin_elf
from shell_scenarios import (
    check_run_from_disk,
    check_startup,
    check_storage_and_shell,
)


DEFAULT_TIMEOUT = 150.0


def run_session(checks, qemu, kernel, disk, timeout):
    session = QemuSession(qemu, kernel, disk, timeout)
    try:
        session.start()
        for check in checks:
            check(session)
    except (IntegrationFailure, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        print(session.diagnostics(), file=sys.stderr)
        raise
    finally:
        session.close()


def run_main_session(qemu, kernel, disk, timeout):
    run_session(
        [
            check_startup,
            check_storage_and_shell,
            check_ring3_and_builtin_elf,
            check_run_from_disk,
            check_page_faults,
            check_user_oom,
        ],
        qemu, kernel, disk, timeout,
    )
    print("PASS: QEMU integration suite", flush=True)


def run_kernel_fault_session(qemu, kernel, disk, timeout):
    # A kernel page fault panics and reboots the machine, so it gets a
    # dedicated session after the main suite has finished.
    run_session([check_kernel_fault_panic], qemu, kernel, disk, timeout)
    print("PASS: kernel-fault panic session", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--kernel", default=str(ROOT / "myos.bin"))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    tempdir = tempfile.TemporaryDirectory(prefix="myos-qemu-")
    disk = Path(tempdir.name) / "disk.img"
    try:
        create_test_disk(tempdir.name)
        run_main_session(args.qemu, args.kernel, disk, args.timeout)
        run_kernel_fault_session(args.qemu, args.kernel, disk, args.timeout)
        print("ALL PASS", flush=True)
        return 0
    except (IntegrationFailure, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: QEMU integration suite: {error}", file=sys.stderr)
        return 1
    finally:
        tempdir.cleanup()


if __name__ == "__main__":
    sys.exit(main())
