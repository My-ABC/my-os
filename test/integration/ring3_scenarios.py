from common import IntegrationFailure


def check_ring3_and_builtin_elf(session):
    session.send_line("ring3")
    session.wait_for("[RING3] Started one background process")
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 1 (?:READY|RUNNING)")
    session.pass_check("Ring 3 scheduling and process listing")

    session.send_line("fg 1")
    session.wait_for("RING3 is RUN, procss_id: 1", timeout=5)
    session.pass_check("foreground Ring 3 system-call output")
    session.send_line("bg 1")
    before = len(session.serial_data)
    session.read_serial(1.3)
    if b"RING3 is RUN, procss_id: 1" in session.serial_data[before:]:
        raise IntegrationFailure("Ring 3 output continued after bg 1")
    session.pass_check("background command suppresses Ring 3 output")
    session.send_line("kill 1")
    session.wait_for("[RING3] Killed PID 1")
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 1 DEAD", start=ps_start)
    if b"[PS] 1 READY" in session.serial_data[ps_start:] or \
            b"[PS] 1 RUNNING" in session.serial_data[ps_start:]:
        raise IntegrationFailure("killed Ring 3 process remains runnable")
    session.pass_check("kill reclaims a Ring 3 process")

    session.send_line("elf")
    session.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
    session.wait_for("Hello from ELF32", timeout=5)
    session.pass_check("ELF32 load, Ring 3 execution, and SYS_WRITE")
    session.send_line("kill 2")
    session.wait_for("[RING3] Killed PID 2")
    session.send_line("elf")
    session.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
    session.wait_for("Hello from ELF32", timeout=5)
    session.pass_check("ELF image mappings can be released and loaded again")
    session.send_line("kill 3")
    session.wait_for("[RING3] Killed PID 3")
    session.pass_check("ELF process termination")
