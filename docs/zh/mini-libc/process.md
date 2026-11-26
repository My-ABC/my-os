# `process.h` + `sys/types.h` — 进程表与前台控制

基于 `SYS_PROCESS/PROC_LIST` 和 `PROC_SET_FG`。内核没有
fork/wait——每个 `run` 都是独立的 Ring 3 进程；本头文件只暴露
ABI 提供的能力。

## 类型

```c
/* sys/types.h */
typedef int pid_t;
typedef int ssize_t;
typedef int32_t off_t;

/* process.h */
enum {
    PROC_ST_UNUSED  = 0,
    PROC_ST_READY   = 1,
    PROC_ST_RUNNING = 2,
    PROC_ST_DEAD    = 3
};

typedef struct {
    pid_t pid;
    int   state;        /* PROC_ST_* */
    int   foreground;   /* 非零表示该进程为前台 */
} proc_info_t;
```

## 函数

### `int proc_list(proc_info_t *buf, int max)`

- **参数**：`buf` — 输出数组；`max` — 记录容量。
- **返回值**：写入的记录数，或 `< 0`。
- **详细说明**：经 `PROC_LIST` 枚举所有进程槽位。内核写入
  12 字节 `{pid, state, is_foreground}` 记录——`proc_info_t`
  布局兼容。未使用槽位以 `pid == 0`、`state == PROC_ST_UNUSED`
  出现。

### `int proc_set_foreground(pid_t pid)`

- **参数**：`pid` — 要提升为前台的进程，或 `0`。
- **返回值**：成功 `0`，失败 `< 0`。
- **详细说明**：把 `pid` 设为前台进程——只有前台进程的控制台
  输出会显示（后台输出被抑制）。`pid == 0` 则把当前前台进程
  **退到后台**。前台槽位按终端独立。

### `pid_t proc_foreground(void)`

- **参数**：无。
- **返回值**：当前前台进程的 pid；无或未知时返回 `-1`。
- **详细说明**：通过 `PROC_LIST` 扫描前台标志实现。

### `int proc_run_demo(void)`

- **参数**：无。
- **返回值**：成功 `0`，失败 `< 0`。
- **详细说明**：在前台运行内核内置的 ELF32 演示映像
  （`PROC_RUN_DEMO`）——等同 shell 的 `elf` 命令。

### `int proc_run_usercode(void)`

- **参数**：无。
- **返回值**：成功 `0`，失败 `< 0`。
- **详细说明**：在前台启动内核内置的 Ring 3 用户态代码演示
  （`PROC_RUN_USERCODE`）——等同 shell 的 `ring3` 命令。
