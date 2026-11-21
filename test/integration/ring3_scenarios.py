import time

from common import IntegrationFailure


def check_ring3_and_builtin_elf(session):
    session.send_line("ring3")
    session.wait_for("[RING3] Started one background process")
    started = time.monotonic()
    session.send_line("secho r3ok")
    session.wait_for("r3ok", timeout=2)
    if time.monotonic() - started >= 2:
        raise IntegrationFailure("shell input stalled while a Ring 3 process ran")
    session.pass_check("shell stays responsive while a Ring 3 process runs")
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 1 (?:READY|RUNNING)")
    session.pass_check("Ring 3 scheduling and process listing")

    session.send_line("fg 1")
    session.wait_for("RING3 is RUN, procss_id: 1", timeout=5)
    session.pass_check("foreground Ring 3 system-call output")
    session.send_line("bg 1")
    session.send_line("secho bg-sentinel")
    session.wait_for("bg-sentinel", timeout=5)
    before = len(session.serial_data)
    session.read_serial(1.3)
    if b"RING3 is RUN, procss_id: 1" in session.serial_data[before:]:
        raise IntegrationFailure("Ring 3 output continued after bg 1")
    session.pass_check("background command suppresses Ring 3 output")
    session.send_line("fg 1")
    # Ctrl+C delivers SIGINT to the foreground process and kills it.
    session.monitor.sendall(b"sendkey ctrl-c\n")
    session.monitor_read_until(b"(qemu)", 3)
    session.wait_for("^C", timeout=5)
    # Note: the redrawn prompt is VGA-only and not visible on serial.
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 1 DEAD", start=ps_start, timeout=8)
    if b"[PS] 1 READY" in session.serial_data[ps_start:] or \
            b"[PS] 1 RUNNING" in session.serial_data[ps_start:]:
        raise IntegrationFailure("SIGINT-killed Ring 3 process remains runnable")
    session.pass_check("Ctrl+C SIGINT terminates the foreground Ring 3 process")

    session.send_line("elf")
    session.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
    session.wait_for("Hello from ELF32", timeout=5)
    session.pass_check("ELF32 load, Ring 3 execution, SYS_WRITE, and SYS_EXIT")
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 2 DEAD", start=ps_start, timeout=5)
    session.send_line("elf")
    session.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
    session.wait_for("Hello from ELF32", timeout=5)
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 3 DEAD", start=ps_start, timeout=5)
    session.pass_check("ELF image mappings can be released and loaded again")
    session.pass_check("ELF process termination")
