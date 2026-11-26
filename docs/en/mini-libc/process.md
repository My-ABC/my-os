# `process.h` + `sys/types.h` — process table and foreground

Over `SYS_PROCESS/PROC_LIST` and `PROC_SET_FG`. The kernel has no
fork/wait — each `run` spawns an independent Ring 3 process; this
header only exposes what the ABI offers.

## Types

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
    int   foreground;   /* nonzero if this is the foreground process */
} proc_info_t;
```

## Functions

### `int proc_list(proc_info_t *buf, int max)`

- **Parameters**: `buf` — output array; `max` — capacity in
  records.
- **Returns**: number of records written, or `< 0` on error.
- **Details**: Enumerates all process slots via `PROC_LIST`. The
  kernel writes 12-byte `{pid, state, is_foreground}` records —
  `proc_info_t` is layout-compatible. Unused slots appear with
  `pid == 0` and `state == PROC_ST_UNUSED`.

### `int proc_set_foreground(pid_t pid)`

- **Parameters**: `pid` — process to promote, or `0`.
- **Returns**: `0` on success, `< 0` on error.
- **Details**: Makes `pid` the foreground process — only the
  foreground process's console output is displayed (background
  output is suppressed). `pid == 0` instead **backgrounds** the
  current foreground process. The foreground slot is per-terminal.

### `pid_t proc_foreground(void)`

- **Parameters**: none.
- **Returns**: pid of the current foreground process, or `-1` if
  none/unknown.
- **Details**: Implemented via `PROC_LIST` scanning for the
  foreground flag.

### `int proc_run_demo(void)`

- **Parameters**: none.
- **Returns**: `0` on success, `< 0` on error.
- **Details**: Runs the kernel's built-in ELF32 demo image in the
  foreground (`PROC_RUN_DEMO`) — equivalent of the shell `elf`
  command.

### `int proc_run_usercode(void)`

- **Parameters**: none.
- **Returns**: `0` on success, `< 0` on error.
- **Details**: Starts the kernel's built-in Ring 3 user-code demo
  in the foreground (`PROC_RUN_USERCODE`) — equivalent of the shell
  `ring3` command.
