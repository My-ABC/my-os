# `signal.h`

`typedef int sig_atomic_t` `typedef void (*sig_t)(int)`
`SIG_ERR SIG_DFL SIG_IGN` `SIGINT=2` `SIGKILL=9` `NSIG=32`.

`sig_t signal(int sig, sig_t func)` — install handler, returns
previous disposition (`SIG_ERR` on failure). Only **SIGINT** is
catchable (delivered on Ctrl+C); SIGKILL and all other numbers
are rejected. Userspace table tracks dispositions.
`int kill(pid_t pid, int sig)` `int raise(int sig)`.

Handler ABI: entered on the user stack with a cdecl frame
(`[esp]=kernel restorer, [esp+4]=sig`), so a plain
`void handler(int sig)` works; `return`ing resumes the
interrupted context via the kernel restorer.
No `sigprocmask`/`sigaction`/`sigaltstack` — the ABI has no
mask support.
