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
- `sig_blocked` — bitmask of blocked signals; a delivered signal is
  blocked for the duration of its handler (no syscall modifies it yet)
- `sig_handler[NSIG]` — disposition per signal: `SIG_DFL`, `SIG_IGN`,
  or a user-space handler address
- `sig_active` / `sig_saved_frame` — the signal whose handler is
  currently running and the interrupted `irq_frame` it resumes into

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
  `ring3_signal_pending_fatal()` and
  `ring3_signal_deliver_current()`, so a process blocked on keyboard
  input is interrupted promptly. Fatal termination reuses the
  `SYS_EXIT` saved-kernel-frame mechanism.

Disposition handling in `sig_actionable()`:

- `SIG_IGN` → the pending bit is cleared.
- `SIG_DFL` on a fatal signal → the process is terminated.
- `SIG_DFL` on an ignore-by-default signal → the pending bit is
  dropped (no such signals are defined yet).
- A custom user handler → the lowest-numbered pending handled signal
  is delivered (see below).

## Handler delivery

When a process has a pending, unblocked signal with a custom handler,
`sig_deliver()` rewrites the `irq_frame` that will resume it:

- The interrupted frame is copied into `sig_saved_frame`.
- On the user stack it places a two-word call frame
  `[esp]` = restorer address, `[esp+4]` = signal number, and points
  `eip`/`useresp` at the handler, so the handler sees an ordinary
  cdecl `handler(sig)` call.
- The restorer is a fixed user page at `0x00900000`
  (`USER_SIG_TRAMPOLINE`, mapped lazily on first delivery) containing
  `mov eax, SYS_SIGRETURN; int 0x80`. When the handler returns, the
  stub restores the saved frame — registers, `eip`, and `useresp`
  included — so execution resumes exactly where it was interrupted.
- The delivered signal is added to `sig_blocked` for the duration of
  the handler and unblocked by `SYS_SIGRETURN`.

Delivery happens from `ring3_signal_scan()` on IRQ0 (for both the
running process's live frame and READY processes' saved frames) and
from the `SYS_READ`/`SYS_GETCHAR` wait loops via
`ring3_signal_deliver_current()`. A signal that becomes pending while
a handler is already running (`sig_active != 0`) is deferred to a
later tick; fatal signals still kill immediately. Delivering into a
syscall frame abandons that syscall — there is no restart or
`EINTR` return.

## Sources of signals

- **Ctrl+C**: the keyboard driver tracks the Ctrl modifier and emits a
  `KEYBOARD_KEY_SIGINT` (0x05) control byte instead of `'c'`. The
  shell input loop consumes it and calls `ring3_signal_send()` with
  `SIGINT` for the foreground process; with no foreground process the
  byte is ignored. The shell echoes `^C` whenever it consumes the
  byte, discards the line being edited, and redraws the prompt once
  no foreground process remains. If a `SYS_READ`/`SYS_GETCHAR` wait loop reads the
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
- **`SYS_SIGRETURN` (24)**: no arguments; restores the context saved
  at handler delivery. Invoked only by the kernel-placed restorer
  stub — user code should never call it directly. Returns the
  restored `eax`, or -1 when no handler is active.

## Current limitations

- Only one handler level is tracked: while `sig_active` is set,
  further handled signals stay pending instead of nesting (fatal
  signals still terminate immediately).
- An interrupted syscall is abandoned rather than restarted or
  reported (`EINTR`-style semantics are not implemented).
- The handler's return value is discarded; `sigreturn` restores the
  interrupted context's `eax`.
- No `sigprocmask`/`sigpending`/`sigsuspend` syscalls; `sig_blocked`
  is managed internally during handler execution only.
- No `wait`/exit-status reporting or IPC.
- Synchronous signals (e.g. a fault signal on page fault) are not
  generated; faults still terminate the process directly.
- `SIGSTOP`/job control is not implemented.
