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
from page_fault_scenarios import check_page_faults
from ring3_scenarios import check_ring3_and_builtin_elf
from shell_scenarios import (
    check_run_from_disk,
    check_startup,
    check_storage_and_shell,
)


DEFAULT_TIMEOUT = 150.0


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
    session = QemuSession(args.qemu, args.kernel, disk, args.timeout)
    try:
        create_test_disk(tempdir.name)
        session.start()
        check_startup(session)
        check_storage_and_shell(session)
        check_ring3_and_builtin_elf(session)
        check_run_from_disk(session)
        check_page_faults(session)
        print("PASS: QEMU integration suite", flush=True)
        print("ALL PASS", flush=True)
        return 0
    except (IntegrationFailure, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: QEMU integration suite: {error}", file=sys.stderr)
        print(session.diagnostics(), file=sys.stderr)
        return 1
    finally:
        session.close()
        tempdir.cleanup()


if __name__ == "__main__":
    sys.exit(main())
