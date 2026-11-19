#!/usr/bin/env python3
"""Bounded headless QEMU smoke/integration test for MyOS."""

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT = 75.0


class IntegrationFailure(RuntimeError):
    pass


class QemuIntegration:
    def __init__(self, qemu, kernel, timeout):
        self.qemu = qemu
        self.kernel = kernel
        self.timeout = timeout
        self.deadline = time.monotonic() + timeout
        self.serial_data = bytearray()
        self.serial_client = None
        self.monitor = None
        self.process = None
        self.qemu_log = tempfile.TemporaryFile(mode="w+b")
        self.tempdir = tempfile.TemporaryDirectory(prefix="myos-qemu-")
        self.monitor_path = str(Path(self.tempdir.name) / "monitor.sock")

    def remaining(self):
        return max(0.0, self.deadline - time.monotonic())

    def check_running(self):
        if self.process.poll() is not None:
            raise IntegrationFailure(
                f"QEMU exited early with status {self.process.returncode}"
            )
        if self.remaining() <= 0:
            raise IntegrationFailure(f"overall timeout ({self.timeout:g}s) expired")

    def connect_unix(self, path):
        end = min(self.deadline, time.monotonic() + 10)
        while time.monotonic() < end:
            self.check_running()
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                client.connect(path)
                client.setblocking(False)
                return client
            except (FileNotFoundError, ConnectionRefusedError):
                client.close()
                time.sleep(0.05)
        raise IntegrationFailure(f"timed out connecting to QEMU monitor: {path}")

    def start(self):
        if not Path(self.kernel).is_file():
            raise IntegrationFailure(f"kernel image does not exist: {self.kernel}")
        qemu_path = shutil.which(self.qemu)
        if qemu_path is None:
            raise IntegrationFailure(
                f"QEMU executable not found: {self.qemu} "
                "(install qemu-system-x86 or set QEMU=...)"
            )

        serial_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        serial_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        serial_listener.bind(("127.0.0.1", 0))
        serial_listener.listen(1)
        serial_listener.settimeout(min(10, self.remaining()))
        serial_port = serial_listener.getsockname()[1]
        command = [
            qemu_path,
            "-machine", "pc,accel=tcg",
            "-m", "128M",
            "-kernel", str(Path(self.kernel).resolve()),
            "-display", "none",
            "-no-reboot",
            "-no-shutdown",
            "-serial", f"tcp:127.0.0.1:{serial_port},server=off,nodelay=on",
            "-monitor", f"unix:{self.monitor_path},server=on,wait=off",
        ]
        self.process = subprocess.Popen(
            command, cwd=ROOT, stdin=subprocess.DEVNULL,
            stdout=self.qemu_log, stderr=subprocess.STDOUT,
        )
        try:
            self.serial_client, _ = serial_listener.accept()
        except socket.timeout as error:
            serial_listener.close()
            raise IntegrationFailure("timed out waiting for QEMU serial connection") from error
        finally:
            serial_listener.close()
        self.serial_client.setblocking(False)

        self.monitor = self.connect_unix(self.monitor_path)
        self.monitor_read_until(b"(qemu)", 5)
        self.pass_check("QEMU started; headless serial and monitor connected")

    def monitor_read_until(self, marker, timeout):
        end = min(self.deadline, time.monotonic() + timeout)
        data = bytearray()
        while time.monotonic() < end:
            self.check_running()
            try:
                chunk = self.monitor.recv(4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                data.extend(chunk)
                if marker in data:
                    return bytes(data)
            else:
                time.sleep(0.01)
        raise IntegrationFailure("timed out waiting for QEMU monitor response")

    def send_key(self, key):
        self.check_running()
        self.monitor.sendall(f"sendkey {key} 35\n".encode("ascii"))
        self.monitor_read_until(b"(qemu)", 2)

    def send_line(self, line):
        key_names = {
            " ": "spc",
            "\n": "ret",
        }
        for char in line + "\n":
            key = key_names.get(char, char)
            if not (key in ("spc", "ret") or re.fullmatch(r"[a-z0-9]", key)):
                raise IntegrationFailure(f"unsupported test key: {char!r}")
            self.send_key(key)
            time.sleep(0.015)

    def read_serial(self, duration):
        end = min(self.deadline, time.monotonic() + duration)
        while time.monotonic() < end:
            self.check_running()
            try:
                chunk = self.serial_client.recv(65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self.serial_data.extend(chunk)
            else:
                time.sleep(0.01)

    def wait_for(self, needle, timeout=10):
        needle = needle.encode("ascii") if isinstance(needle, str) else needle
        end = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < end:
            self.check_running()
            try:
                chunk = self.serial_client.recv(65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self.serial_data.extend(chunk)
                if needle in self.serial_data:
                    return
            else:
                time.sleep(0.01)
        raise IntegrationFailure(f"serial output did not contain {needle!r}")

    def wait_for_regex(self, pattern, timeout=10, start=0):
        expression = re.compile(pattern)
        end = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < end:
            self.check_running()
            if expression.search(self.serial_data[start:]):
                return
            try:
                chunk = self.serial_client.recv(65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self.serial_data.extend(chunk)
            else:
                time.sleep(0.01)
        raise IntegrationFailure(f"serial output did not match {pattern!r}")

    def serial_text(self):
        return self.serial_data.decode("ascii", errors="replace")

    def pass_check(self, name):
        print(f"PASS: {name}", flush=True)

    def run_checks(self):
        self.wait_for("MyOS v0.0.1", timeout=15)
        self.wait_for("[HEAP] calloc/realloc succeeded", timeout=5)
        self.wait_for("Beijing time (UTC+8)", timeout=5)
        self.pass_check("kernel startup, memory setup, and timer/RTC path reached")
        # start_shell prints only on VGA; COM1 is used to prove command execution.
        self.read_serial(0.3)

        self.send_line("secho qemushellcommandok")
        self.wait_for("qemushellcommandok")
        self.pass_check("keyboard-driven shell command executes")

        self.send_line("ring3")
        self.wait_for("[RING3] Started one background process")
        self.send_line("ps")
        self.wait_for_regex(rb"\[PS\] 1 (?:READY|RUNNING)")
        self.pass_check("Ring 3 scheduling and process listing")

        self.send_line("fg 1")
        self.wait_for("RING3 is RUN, procss_id: 1", timeout=5)
        self.pass_check("foreground Ring 3 system-call output")
        self.send_line("bg 1")
        before = len(self.serial_data)
        self.read_serial(1.3)
        if b"RING3 is RUN, procss_id: 1" in self.serial_data[before:]:
            raise IntegrationFailure("Ring 3 output continued after bg 1")
        self.pass_check("background command suppresses Ring 3 output")
        self.send_line("kill 1")
        self.wait_for("[RING3] Killed PID 1")
        ps_start = len(self.serial_data)
        self.send_line("ps")
        self.wait_for_regex(rb"\[PS\] 1 DEAD", start=ps_start)
        if re.search(rb"\[PS\] 1 (?:READY|RUNNING)", self.serial_data[ps_start:]):
            raise IntegrationFailure("killed Ring 3 process remains runnable")
        self.pass_check("kill reclaims a Ring 3 process")

        self.send_line("elf")
        self.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
        self.wait_for("Hello from ELF32", timeout=5)
        self.pass_check("ELF32 load, Ring 3 execution, and SYS_WRITE")
        self.send_line("kill 2")
        self.wait_for("[RING3] Killed PID 2")
        self.send_line("elf")
        self.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
        self.wait_for("Hello from ELF32", timeout=5)
        self.pass_check("ELF image mappings can be released and loaded again")
        self.send_line("kill 3")
        self.wait_for("[RING3] Killed PID 3")
        self.pass_check("ELF process termination")

    def diagnostics(self):
        self.qemu_log.flush()
        self.qemu_log.seek(0)
        log = self.qemu_log.read().decode("utf-8", errors="replace")
        return (
            f"\n--- recent serial output ---\n{self.serial_text()[-5000:]}"
            f"\n--- QEMU log ---\n{log[-3000:]}"
        )

    def close(self):
        for client in (self.monitor, self.serial_client):
            if client is not None:
                client.close()
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.qemu_log.close()
        self.tempdir.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-i386"))
    parser.add_argument("--kernel", default=str(ROOT / "myos.bin"))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    test = QemuIntegration(args.qemu, args.kernel, args.timeout)
    try:
        test.start()
        test.run_checks()
        print("PASS: QEMU integration suite", flush=True)
        return 0
    except (IntegrationFailure, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: QEMU integration suite: {error}", file=sys.stderr)
        print(test.diagnostics(), file=sys.stderr)
        return 1
    finally:
        test.close()


if __name__ == "__main__":
    sys.exit(main())
