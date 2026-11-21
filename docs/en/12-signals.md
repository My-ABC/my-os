# 12 — Signals

MyOS implements a basic signal mechanism for Ring 3 processes.
Signal numbers are defined in `include/os/signal.h`.

## Signal numbers

Only two signals are implemented so far; other numbers in the 1–31
range are reserved:

| Number | Name | Default action |
|--------|------|----------------|
| 2 | `SIGINT` | terminate (sent by Ctrl+C) |
| 9 | `SIGKILL` | terminate, cannot be caught or blocked |

`NSIG` is 32 so the pending/blocked bitmasks and handler table have
room to grow. `SIG_DFL` (0) selects the default action and `SIG_IGN`
(1) ignores the signal. `SIGKILL` is uncatchable
(`SIG_UNCATCHABLE`); there are no ignore-by-default signals yet.

## Per-process state

Each `struct ring3_process` carries:

- `sig_pending` — bitmask of signals sent but not yet acted on
- `sig_blocked` — bitmask of blocked signals (reserved; no syscall
  modifies it yet)
- `sig_handler[NSIG]` — disposition per signal: `SIG_DFL`, `SIG_IGN`,
  or a user-space handler address

State is reset every time a process slot is scheduled.

## Delivery

Delivery is poll-based at safe points rather than interrupt-driven:

- `ring3_signal_scan()` runs on every PIT tick (IRQ0) inside
  `ring3_timer_switch`. A process with a fatal, unblocked, pending
  signal is marked `DEAD`; the normal context-switch path then reaps
  it. The currently running process is only marked when the
  interrupted frame is a user frame — a kernel frame means it is
  inside a syscall.
- `SYS_READ`/`SYS_GETCHAR` use signal-aware wait loops that poll
  `ring3_signal_pending_fatal()`, so a process blocked on keyboard
  input is also killed promptly. Termination reuses the `SYS_EXIT`
  saved-kernel-frame mechanism.

Disposition handling in `sig_deliverable_mask()`:

- `SIG_IGN` → the pending bit is cleared.
- `SIG_DFL` on a fatal signal → the process is terminated.
- `SIG_DFL` on an ignore-by-default signal → the pending bit is
  dropped (no such signals are defined yet).
- A custom user handler → the pending bit stays set; **user-mode
  handler invocation is not implemented yet**, so the signal remains
  pending until the process exits.

## Sources of signals

- **Ctrl+C**: the keyboard driver tracks the Ctrl modifier and emits a
  `KEYBOARD_KEY_SIGINT` (0x05) control byte instead of `'c'`. The
  shell input loop consumes it and calls `ring3_signal_send()` with
  `SIGINT` for the foreground process; with no foreground process the
  byte is ignored. If a `SYS_READ`/`SYS_GETCHAR` wait loop reads the
  byte first, it delivers `SIGINT` to itself instead, so a process
  blocked on keyboard input is still interrupted (subject to its
  disposition).
- **`SYS_KILL` (22)**: `ebx` = pid, `ecx` = signal number; marks the
  signal pending on the target process. Only `SIGINT`/`SIGKILL` are
  accepted; reserved numbers return -1. Returns 0 or -1.
- **`SYS_SIGNAL` (23)**: `ebx` = signal number, `ecx` = handler
  (0 = default, 1 = ignore, otherwise a user address below
  `USER_SPACE_END`). Sets the disposition for the *current* process.
  `SIGKILL` cannot be caught; reserved numbers return -1.
  Returns 0 or -1.

## Current limitations

- Custom signal handlers are stored but never invoked; pending custom
  signals accumulate.
- No `sigprocmask`/`sigpending`/`sigsuspend` syscalls; `sig_blocked`
  exists but nothing sets it.
- No `wait`/exit-status reporting or IPC.
- Synchronous signals (e.g. a fault signal on page fault) are not
  generated; faults still terminate the process directly.
- `SIGSTOP`/job control is not implemented.
