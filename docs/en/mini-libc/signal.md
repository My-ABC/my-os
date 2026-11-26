# `signal.h` — signals

Over `SYS_SIG`. The kernel implements exactly two signals:
`SIGINT` (catchable, delivered on Ctrl+C) and `SIGKILL`
(uncatchable, always fatal). There is no `sigprocmask`/`sigaction`
mask support — `signal()` is the whole interface.

## Types and constants

```c
typedef int sig_atomic_t;
typedef void (*sig_t)(int);

#define SIG_ERR ((sig_t)-1)
#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)

#define SIGINT  2
#define SIGKILL 9
#define NSIG    32
```

## Functions

### `sig_t signal(int sig, sig_t func)`

- **Parameters**: `sig` — signal number; `func` — handler function,
  `SIG_DFL` (default), or `SIG_IGN` (ignore).
- **Returns**: previous disposition, or `SIG_ERR` on error.
- **Details**: Only `SIGINT` is catchable; installing a handler or
  `SIG_IGN` for `SIGKILL` fails. A caught handler runs on the user
  stack with a cdecl frame (`[esp]=restorer`, `[esp+4]=sig`), so a
  plain `void handler(int sig)` works; returning from it resumes
  the interrupted context automatically. Handlers must be
  async-signal-safe in practice — the interrupted context may have
  been inside any syscall.

### `int kill(pid_t pid, int sig)`

- **Parameters**: `pid` — target process; `sig` — `SIGINT` or
  `SIGKILL`.
- **Returns**: `0` on success, `< 0` on error (`ESRCH` for dead/ bad
  pid).
- **Details**: `SIGKILL` is uncatchable and fatal. Signals are
  delivered at the target's next signal checkpoint (a blocking
  syscall); a signal sent to the **current** process is checked
  synchronously before the syscall returns.

### `int raise(int sig)`

- **Parameters**: `sig` — signal number.
- **Returns**: `0` on success, `< 0` on error.
- **Details**: Equivalent to `kill(getpid(), sig)`.
