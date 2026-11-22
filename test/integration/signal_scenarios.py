from common import IntegrationFailure


def check_signal_handler_delivery(session):
    """Ctrl+C must invoke the user-registered SIGINT handler and resume
    the interrupted context via SIG_RETURN."""
    sig_start = len(session.serial_data)
    session.send_line("run sigdemo.elf")
    session.wait_for_regex(rb"\[ELF\] Loaded ELF32 entry=", start=sig_start,
                           timeout=8)
    session.wait_for_regex(rb"SIGDEMO-ARMED", start=sig_start, timeout=8)

    # Ctrl+C -> shell sends SIGINT to the foreground process; the next
    # timer tick (or syscall wait) delivers it to sig_handler.
    session.monitor.sendall(b"sendkey ctrl-c\n")
    session.monitor_read_until(b"(qemu)", 3)
    session.wait_for_regex(rb"\[SIG\] Deliver sig 2", start=sig_start,
                           timeout=8)
    session.wait_for_regex(rb"SIGDEMO-HANDLED", start=sig_start, timeout=8)
    session.wait_for_regex(rb"\[SIG\] sigreturn", start=sig_start, timeout=8)
    session.wait_for_regex(rb"SIGDEMO-AFTER", start=sig_start, timeout=8)
    session.wait_for_regex(rb"SIGDEMO-DONE", start=sig_start, timeout=8)
    if b"SIGDEMO-FAIL" in session.serial_data[sig_start:]:
        raise IntegrationFailure("sigdemo.elf reported SIGDEMO-FAIL")

    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 9 DEAD", start=ps_start, timeout=8)
    session.pass_check("SIGINT handler delivery and sigreturn context restore")
