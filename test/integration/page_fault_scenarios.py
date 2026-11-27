import re
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
    # heap.elf (P1-1 smoke): the QEMU test disk has no shell3.elf, so
    # commands run on the ring0 kernel shell and only one foreground
    # process may print to COM3 — a concurrent two-vt check is not
    # possible here. Run the malloc/write/free/exit demo twice
    # sequentially and assert both complete without a panic. The
    # concurrent variant runs in check_heap_vt_isolation on a disk
    # that actually contains shell3.elf.
    start = len(session.serial3_data)
    for _ in range(2):
        session.send_line("run heap.elf")
    session.wait_for_regex(rb"HEAP-OK(?s:.)*HEAP-OK", timeout=15)
    tail = bytes(session.serial3_data[start:])
    if b"HEAP-FAIL" in tail:
        raise IntegrationFailure("heap.elf reported HEAP-FAIL")
    if tail.count(b"HEAP-OK") != 2:
        raise IntegrationFailure(
            f"expected 2 HEAP-OK lines, got {tail.count(b'HEAP-OK')}")
    session.pass_check("user heap malloc/write/free/exit path works twice")


def check_heap_vt_isolation(session):
    # heap.elf (P1-1): on a disk with shell3.elf both vts have real
    # Ring 3 shells. Two concurrent instances must not share user-heap
    # free-list/bump-pointer state: each mallocs, writes a pid-derived
    # magic, frees a small block, yields so the peer publishes its own
    # freed block, allocates again, and verifies the magic survived.
    session.wait_for("[VT] shell3 started on vt 0", timeout=20)
    session.wait_for("[VT] shell3 started on vt 1", timeout=20)
    start = len(session.serial3_data)
    session.send_line("run heap.elf")          # vt0 shell3
    time.sleep(0.5)                            # let it reach the yield loop
    session.send_key("f2")                     # switch to vt1
    time.sleep(0.8)                            # vt_service is deferred
    session.send_line("run heap.elf")          # vt1 shell3
    session.wait_for_regex(rb"HEAP-OK(?s:.)*HEAP-OK", timeout=25)
    tail = bytes(session.serial3_data[start:])
    if b"HEAP-FAIL" in tail:
        raise IntegrationFailure("heap.elf reported HEAP-FAIL")
    if tail.count(b"HEAP-OK") != 2:
        raise IntegrationFailure(
            f"expected 2 HEAP-OK lines, got {tail.count(b'HEAP-OK')}")
    session.pass_check("per-process user heaps isolate concurrent mallocs")
    session.send_key("f1")                     # back to vt0


def check_sleep_vt_switch(session):
    # R3-N2: TIME_SLEEP must park the process in SLEEPING so the idle
    # loop keeps servicing vt_pending. Run sleep.elf (500 ms) on vt0,
    # press F2 immediately, and require the switch well before the
    # sleep ends — pre-fix it only happened after the process exited.
    session.wait_for("[VT] shell3 started on vt 0", timeout=20)
    session.wait_for("[VT] shell3 started on vt 1", timeout=20)
    session.send_key("f1")
    session.wait_for("[VT] switched to vt 0", timeout=5)
    session.send_line("run sleep.elf")
    time.sleep(0.1)
    start = time.monotonic()
    session.send_key("f2")
    session.wait_for("[VT] switched to vt 1", timeout=5)
    elapsed = time.monotonic() - start
    if elapsed > 0.45:
        raise IntegrationFailure(
            f"vt switch took {elapsed:.2f}s during TIME_SLEEP")
    session.pass_check(f"F2 switches vt during TIME_SLEEP ({elapsed*1000:.0f} ms)")
    session.send_key("f1")                     # back to vt0
    session.wait_for("[VT] switched to vt 0", timeout=5)
    session.wait_for_regex(rb"SLEEP-OK", timeout=10)
    session.pass_check("sleep.elf woke and completed")

    # Ctrl+C must kill a process parked in SLEEPING. shell3 `ps`
    # prints to COM3; capture a fresh offset so stale DEAD rows from
    # the earlier heap runs do not satisfy the check.
    session.send_line("run sleep.elf")
    time.sleep(0.3)
    session.monitor.sendall(b"sendkey ctrl-c\n")
    time.sleep(0.5)
    ps_start = len(session.serial3_data)
    session.send_line("ps")
    # wait_for_regex cannot offset into serial3_data, so poll the
    # COM3 buffer slice directly.
    deadline = time.monotonic() + 8
    found = False
    while time.monotonic() < deadline:
        session._read_serial_clients()
        if re.search(rb"\d+\s+DEAD",
                     bytes(session.serial3_data[ps_start:])):
            found = True
            break
        time.sleep(0.05)
    if not found:
        raise IntegrationFailure(
            "sleeping process not reaped after Ctrl+C")
    session.pass_check("Ctrl+C kills a process sleeping in TIME_SLEEP")


def check_iowait_vt_switch(session):
    # R3-N3: a process blocked in IO_READ *mid-line* (chars typed, no
    # Enter) must yield so vt_service keeps running — pre-fix it hlt'd
    # in kernel context and F2 could not switch until Enter.
    session.send_key("f1")
    session.wait_for("[VT] switched to vt 0", timeout=5)
    session.send_line("run iowait.elf")
    time.sleep(0.5)                          # let it block in IO_READ
    session.send_text("ab")                  # partial line: i=2, no Enter
    time.sleep(0.3)
    start = time.monotonic()
    session.send_key("f2")
    session.wait_for("[VT] switched to vt 1", timeout=5)
    elapsed = time.monotonic() - start
    if elapsed > 0.3:
        raise IntegrationFailure(
            f"vt switch took {elapsed:.2f}s during mid-line IO_READ")
    session.pass_check(f"F2 switches vt during mid-line IO_READ ({elapsed*1000:.0f} ms)")
    session.send_key("f1")
    session.wait_for("[VT] switched to vt 0", timeout=5)
    session.send_text("cde")
    session.send_named_key("ret")            # completes "abcde"
    session.wait_for_regex(rb"LINE-OK", timeout=8)
    session.pass_check("mid-line IO_READ resumed and completed the line")


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
