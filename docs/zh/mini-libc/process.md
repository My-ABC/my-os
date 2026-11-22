# `process.h` + `sys/types.h`

`proc_info_t { pid_t pid; int state; int foreground; }`
`PROC_ST_UNUSED/READY/RUNNING/DEAD`。

`int proc_list(proc_info_t *buf, int max)` —— 枚举进程；
返回数量或 <0。
`int proc_set_foreground(pid_t pid)` —— pid 0 将当前前台
进程转为后台。
`pid_t proc_foreground(void)` —— 前台 pid 或 -1。

无 fork/wait/exec 系（unistd.h 仅 `execve`）——每个
`run` 是独立 Ring 3 进程。
