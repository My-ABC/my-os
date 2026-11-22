# `unistd.h` + `sys/types.h`

`sys/types.h`: `pid_t` (int), `ssize_t` (int), `off_t` (int).

Process: `_exit(status)` (no atexit), `pid_t getpid()`,
`int sched_yield()`, `int execve(path,argv,envp)` (PROC_EXEC).
fd I/O: `ssize_t read(fd,buf,size)` `ssize_t write(fd,buf,size)`
`int close(fd)`.
Paths: `int unlink(path)` `int rmdir(path)` `int chdir(path)`
`int getcwd(buf,size)` `int access(path,mode)`.

`fork`/`getppid`/`lseek` return `-1` with `errno=ENOSYS`
(no kernel support).
