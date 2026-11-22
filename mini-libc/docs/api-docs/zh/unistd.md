# `unistd.h` + `sys/types.h`

`sys/types.h`：`pid_t`（int）、`ssize_t`（int）、`off_t`（int）。

进程：`_exit(status)`（不跑 atexit）、`pid_t getpid()`、
`int sched_yield()`、`int execve(path,argv,envp)`（PROC_EXEC）。
fd I/O：`ssize_t read(fd,buf,size)` `ssize_t write(fd,buf,size)`
`int close(fd)`。
路径：`int unlink(path)` `int rmdir(path)` `int chdir(path)`
`int getcwd(buf,size)` `int access(path,mode)`。

`fork`/`getppid`/`lseek` 返回 `-1` 且 `errno=ENOSYS`
（内核不支持）。
