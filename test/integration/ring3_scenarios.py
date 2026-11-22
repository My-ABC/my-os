from common import IntegrationFailure


def check_ring3_and_builtin_elf(session):
    session.send_line("ring3")
    session.wait_for("[RING3] Started one foreground process")
    session.wait_for("RING3 is RUN, procss_id: 1", timeout=5)
    session.pass_check("foreground Ring 3 process output")

    # The child never reads keys, so shell input typed now stays
    # buffered; it must still execute once the child exits.
    session.send_line("echo typeahead-ok")
    # Ctrl+C is turned into SIGINT by the keyboard ISR itself, so it
    # reaches the foreground process even though nothing reads the
    # buffered control byte while the child owns the console.
    session.monitor.sendall(b"sendkey ctrl-c\n")
    session.monitor_read_until(b"(qemu)", 3)
    session.wait_for("typeahead-ok", timeout=8)
    session.wait_for("^C", timeout=5)
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 1 DEAD", start=ps_start, timeout=8)
    if b"[PS] 1 READY" in session.serial_data[ps_start:] or \
            b"[PS] 1 RUNNING" in session.serial_data[ps_start:]:
        raise IntegrationFailure("SIGINT-killed Ring 3 process remains runnable")
    session.pass_check("Ctrl+C SIGINT terminates the foreground Ring 3 "
                       "process and buffered input still runs")

    # The built-in ELF demo installs a SIGINT handler that prints "Bye"
    # and exits; Ctrl+C must reach it through the signal mechanism.
    for expected_pid in (b"2", b"3"):
        elf_start = len(session.serial_data)
        session.send_line("elf")
        session.wait_for("[ELF] Loaded ELF32 entry=", timeout=8)
        session.wait_for("Hello from ELF32", timeout=5)
        session.monitor.sendall(b"sendkey ctrl-c\n")
        session.monitor_read_until(b"(qemu)", 3)
        session.wait_for_regex(rb"\[SIG\] Deliver sig 2", start=elf_start,
                               timeout=8)
        session.wait_for("Bye", timeout=5)
        ps_start = len(session.serial_data)
        session.send_line("ps")
        session.wait_for_regex(rb"\[PS\] " + expected_pid + rb" DEAD",
                               start=ps_start, timeout=8)
    session.pass_check("ELF32 load, Ring 3 execution, SIGINT handler, "
                       "and PROC_EXIT")
