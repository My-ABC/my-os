from common import IntegrationFailure


def check_page_faults(session):
    # stack.elf grows its user stack past the pre-mapped page; each new
    # page must be demand-mapped by the page-fault handler.
    stack_start = len(session.serial_data)
    session.send_line("run stack.elf")
    session.wait_for_regex(rb"STACK-OK", start=stack_start, timeout=8)
    if b"Demand-mapped user page" not in \
            session.serial_data[stack_start:]:
        raise IntegrationFailure("user stack growth did not demand-map pages")
    session.pass_check("user stack growth demand-maps registered pages")

    # faultu.elf touches an unregistered user address; the kernel must kill
    # the process and keep the shell alive.
    unmapped_start = len(session.serial_data)
    session.send_line("run faultu.elf")
    session.wait_for_regex(rb"UNMAPPED-NEXT", start=unmapped_start,
                            timeout=8)
    session.wait_for_regex(
        rb"\[PAGE FAULT\] Illegal user page fault", start=unmapped_start,
        timeout=5,
    )
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 6 DEAD", start=ps_start, timeout=5)
    session.pass_check("unregistered user access terminates the process")

    # faultp.elf writes to its own read-only code page; a protection
    # violation must also terminate only that process.
    prot_start = len(session.serial_data)
    session.send_line("run faultp.elf")
    session.wait_for_regex(rb"PROT-NEXT", start=prot_start, timeout=8)
    session.wait_for_regex(
        rb"\[PAGE FAULT\] User protection violation", start=prot_start,
        timeout=5,
    )
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 7 DEAD", start=ps_start, timeout=5)
    session.pass_check("user protection violation terminates the process")

    # The still-running stack.elf process must remain schedulable, and the
    # shell must still accept commands.
    session.send_line("kill 5")
    session.wait_for("[RING3] Killed PID 5")
    session.send_line("secho page-fault-suite-ok")
    session.wait_for("page-fault-suite-ok")
    session.pass_check("shell survives page-fault process termination")
