import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from common import IntegrationFailure, ROOT


class QemuSession:
    def __init__(self, qemu, kernel, disk, timeout):
        self.qemu = qemu
        self.kernel = kernel
        self.disk = disk
        self.timeout = timeout
        self.deadline = time.monotonic() + timeout
        self.serial_data = bytearray()
        self.serial3_data = bytearray()  # COM3: Ring 3 IO_WRITE mirror
        self.serial_client = None
        self.serial3_client = None
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

    def connect_monitor(self):
        end = min(self.deadline, time.monotonic() + 10)
        while time.monotonic() < end:
            self.check_running()
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                client.connect(self.monitor_path)
                client.setblocking(False)
                return client
            except (FileNotFoundError, ConnectionRefusedError):
                client.close()
                time.sleep(0.05)
        raise IntegrationFailure(
            f"timed out connecting to QEMU monitor: {self.monitor_path}"
        )

    def start(self):
        if self.remaining() <= 0:
            raise IntegrationFailure(f"overall timeout ({self.timeout:g}s) expired")
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
        # COM1 carries kernel diagnostics; COM3 mirrors Ring 3 IO_WRITE
        # output. COM2 gets a 'none' backend so COM3 lands on the third
        # -serial argument.
        serial3_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        serial3_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        serial3_listener.bind(("127.0.0.1", 0))
        serial3_listener.listen(1)
        serial3_listener.settimeout(min(10, self.remaining()))
        serial3_port = serial3_listener.getsockname()[1]
        command = [
            qemu_path,
            "-machine", "pc,accel=tcg",
            "-rtc", "base=utc",
            "-m", "128M",
            "-kernel", str(Path(self.kernel).resolve()),
            "-display", "none",
            "-no-reboot",
            "-no-shutdown",
            "-drive", f"file={self.disk},format=raw,if=ide",
            "-serial", f"tcp:127.0.0.1:{serial_port},server=off,nodelay=on",
            "-serial", "none",
            "-serial", f"tcp:127.0.0.1:{serial3_port},server=off,nodelay=on",
            "-monitor", f"unix:{self.monitor_path},server=on,wait=off",
        ]
        try:
            self.process = subprocess.Popen(
                command, cwd=ROOT,
                stdin=subprocess.DEVNULL, stdout=self.qemu_log,
                stderr=subprocess.STDOUT,
            )
            self.serial_client, _ = serial_listener.accept()
            self.serial3_client, _ = serial3_listener.accept()
        except socket.timeout as error:
            raise IntegrationFailure(
                "timed out waiting for QEMU serial connection"
            ) from error
        finally:
            serial_listener.close()
            serial3_listener.close()
        self.serial_client.setblocking(False)
        self.serial3_client.setblocking(False)

        self.monitor = self.connect_monitor()
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

    @staticmethod
    def key_name(char):
        key_names = {
            " ": "spc",
            ".": "dot",
            "-": "minus",
            "_": "shift-minus",
            "/": "slash",
            '"': "shift-apostrophe",
            "\\": "backslash",
            "\n": "ret",
        }
        return key_names.get(char, char)

    def send_named_key(self, key):
        if not (key in ("spc", "dot", "minus", "shift-minus",
                        "slash", "ret", "left", "right", "up", "down",
                        "backspace", "shift-apostrophe", "backslash") or
                re.fullmatch(r"[a-z0-9]", key)):
            raise IntegrationFailure(f"unsupported test key: {key!r}")
        self.send_key(key)
        time.sleep(0.015)

    def send_text(self, text):
        for char in text:
            self.send_named_key(self.key_name(char))

    def send_line(self, line):
        self.send_text(line + "\n")

    def _read_serial_clients(self):
        """Drain both UART sockets into their own buffers: COM1 kernel
        diagnostics -> serial_data, COM3 Ring 3 mirror -> serial3_data.
        Kept separate because the two streams interleave mid-line and
        would corrupt substring matching if merged. Returns True when
        any bytes arrived."""
        got = False
        for client, buf in ((self.serial_client, self.serial_data),
                            (self.serial3_client, self.serial3_data)):
            try:
                chunk = client.recv(65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                buf.extend(chunk)
                got = True
        return got

    def read_serial(self, duration):
        end = min(self.deadline, time.monotonic() + duration)
        while time.monotonic() < end:
            self.check_running()
            if not self._read_serial_clients():
                time.sleep(0.01)

    def wait_for(self, needle, timeout=10):
        needle = needle.encode("ascii") if isinstance(needle, str) else needle
        end = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < end:
            self.check_running()
            # The needle may already sit in the buffer: a previous wait can
            # consume a chunk that also carries this wait's output, and the
            # guest then goes idle. Re-check every iteration, not only when
            # new bytes arrive.
            if needle in self.serial_data or needle in self.serial3_data:
                return
            if not self._read_serial_clients():
                time.sleep(0.01)
        raise IntegrationFailure(f"serial output did not contain {needle!r}")

    def wait_for_regex(self, pattern, timeout=10, start=0):
        expression = re.compile(pattern)
        end = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < end:
            self.check_running()
            if (expression.search(self.serial_data[start:]) or
                    expression.search(self.serial3_data)):
                return
            if not self._read_serial_clients():
                time.sleep(0.01)
        raise IntegrationFailure(f"serial output did not match {pattern!r}")

    def serial_text(self):
        return self.serial_data.decode("ascii", errors="replace")

    def serial_all_text(self):
        """COM1 + COM3 concatenated. COM1 keeps kernel-side output,
        COM3 carries Ring 3 IO_WRITE mirrors; each stream stays
        line-intact but their relative interleaving is lost."""
        return (bytes(self.serial_data) + b"\n" +
                bytes(self.serial3_data)).decode("ascii", errors="replace")

    @staticmethod
    def pass_check(name):
        print(f"✓ PASS: {name}", flush=True)

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
