import time

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

    # stack.elf halts forever in the foreground, so the console belongs
    # to it: Ctrl+C (turned into SIGINT by the keyboard ISR) is the only
    # way back to the shell.
    session.monitor.sendall(b"sendkey ctrl-c\n")
    session.monitor_read_until(b"(qemu)", 3)
    session.wait_for("^C", timeout=5)
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 5 DEAD", start=ps_start, timeout=8)
    session.pass_check("Ctrl+C reaps the halted foreground stack.elf")

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

    # The shell must still accept commands after the fault kills.
    session.send_line("echo page-fault-suite-ok")
    session.wait_for("page-fault-suite-ok")
    session.pass_check("shell survives page-fault process termination")


def check_bufcheck(session):
    # bufcheck.elf (R3-N1): syscall buffers in the borrowed low-4MiB
    # supervisor identity map must be rejected with -1; legal user
    # buffers must still work. It halts in the foreground afterwards,
    # so Ctrl+C is needed to return to the shell.
    start = len(session.serial3_data)
    session.send_line("run bufcheck.elf")
    session.wait_for_regex(rb"BADW-REJ", timeout=10)
    session.wait_for_regex(rb"BADR-REJ", timeout=5)
    session.wait_for_regex(rb"GOODW-OK", timeout=5)
    if b"BADW-FAIL" in bytes(session.serial3_data[start:]) or \
            b"BADR-FAIL" in bytes(session.serial3_data[start:]) or \
            b"GOODW-FAIL" in bytes(session.serial3_data[start:]):
        raise IntegrationFailure("bufcheck reported a validation failure")
    session.pass_check("syscall buffers in borrowed supervisor range rejected")

    # The legal IO_READ blocks for a line; feed it one.
    session.send_line("hi")
    session.wait_for_regex(rb"GOODR-OK", timeout=8)
    session.pass_check("legal writable user buffer accepted by IO_READ")

    session.monitor.sendall(b"sendkey ctrl-c\n")
    session.monitor_read_until(b"(qemu)", 3)
    session.wait_for("^C", timeout=5)
    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 8 DEAD", start=ps_start, timeout=8)
    session.pass_check("bufcheck.elf reaped after Ctrl+C")


def check_heap_isolation(session):
    # heap.elf (P1-1): two concurrent instances on vt0 and vt1 must not
    # share user-heap free-list/bump-pointer state. Each mallocs, frees
    # a small block, yields so the peer publishes its own freed block,
    # allocates again, and verifies its magic survived.
    start = len(session.serial3_data)
    session.send_line("run heap.elf")          # vt0
    session.send_key("f2")                     # switch to vt1
    time.sleep(0.5)                            # vt_service is deferred
    session.send_line("run heap.elf")          # vt1
    session.wait_for_regex(rb"HEAP-OK(?s:.)*HEAP-OK", timeout=20)
    tail = bytes(session.serial3_data[start:])
    if b"HEAP-FAIL" in tail:
        raise IntegrationFailure("heap.elf reported HEAP-FAIL")
    if tail.count(b"HEAP-OK") != 2:
        raise IntegrationFailure(
            f"expected 2 HEAP-OK lines, got {tail.count(b'HEAP-OK')}")
    session.pass_check("per-process user heaps isolate concurrent mallocs")

    session.send_key("f1")                     # back to vt0


def check_user_oom(session):
    # oom.elf drains the user heap through SYS_MEMORY until the PMM is
    # empty, then touches permitted-but-unmapped heap pages. The demand
    # mapper must fail and kill only that process. Heap blocks leak on
    # process death, so this scenario must stay last in the session: no
    # later check may need fresh physical pages.
    oom_start = len(session.serial_data)
    session.send_line("run oom.elf")
    session.wait_for_regex(rb"OOM-NEXT", start=oom_start, timeout=15)
    session.wait_for_regex(
        rb"\[PAGE FAULT\] Out of memory for user page",
        start=oom_start, timeout=60,
    )
    session.pass_check("out-of-memory demand-map fault terminates the process")

    ps_start = len(session.serial_data)
    session.send_line("ps")
    session.wait_for_regex(rb"\[PS\] 13 DEAD", start=ps_start, timeout=5)
    session.pass_check("oom.elf is reaped after the out-of-memory kill")

    session.send_line("echo oom-suite-ok")
    session.wait_for("oom-suite-ok")
    session.pass_check("shell survives the out-of-memory termination")


def check_kernel_fault_panic(session):
    # kfault.elf passes an unregistered user pointer to MEMORY_REALLOC;
    # user_realloc dereferences the block header in kernel mode with no
    # validation, which must panic. (IO_WRITE/IO_READ now validate user
    # pointers and return -1, so they can no longer trigger this.)
    # A panic reboots the machine, so this scenario needs a fresh session
    # and must run after all other checks.
    session.wait_for("MyOS v0.0.1", timeout=15)
    session.wait_for(
        "[DISK] ATA primary master and FAT32 filesystem initialized",
        timeout=10,
    )
    session.read_serial(0.5)
    session.send_line("echo panic-suite-ready")
    session.wait_for("panic-suite-ready")

    panic_start = len(session.serial_data)
    session.send_line("run kfault.elf")
    session.wait_for_regex(rb"KFAULT-NEXT", start=panic_start, timeout=10)
    session.wait_for("*** STOP: KERNEL PANIC ***", timeout=10)
    session.wait_for_regex(
        rb"Illegal kernel page fault", start=panic_start, timeout=10,
    )
    session.pass_check("kernel-mode page fault panics with a register dump")
