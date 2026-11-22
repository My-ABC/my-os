# `process.h` + `sys/types.h`

`proc_info_t { pid_t pid; int state; int foreground; }`
`PROC_ST_UNUSED/READY/RUNNING/DEAD`.

`int proc_list(proc_info_t *buf, int max)` — enumerate processes;
returns count or <0.
`int proc_set_foreground(pid_t pid)` — pid 0 backgrounds the
current foreground process.
`pid_t proc_foreground(void)` — fg pid or -1.

No fork/wait/exec-family beyond `execve` in unistd.h — each
`run` is an independent Ring 3 process.
