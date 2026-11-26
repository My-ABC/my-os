# `console.h` — 基于 `SYS_IO` 的控制台原语

交互式行编辑的底层构件：阻塞按键读取、清屏、光标控制。`stdio`
的 `getchar`/`putchar` 建立在同一组 op 之上；需要光标控制或原始
按键时使用这里的接口。

## `int con_getchar(void)`

- **参数**：无。
- **返回值**：下一个按键字节；等待被信号中止时返回 `-1`（检查
  `errno`/信号处置）。
- **详细说明**：阻塞直到有按键。返回原始内核字节——`0x03`
  （Ctrl+C 标记）等控制字节原样透传，需要自行映射。

## `void con_clear(void)`

- **参数**：无。
- **返回值**：无。
- **详细说明**：通过 `SYS_IO/IO_CLEAR` 清屏并将光标归位。

## `int con_cursor_get(uint16_t *row, uint16_t *col)`

- **参数**：`row`、`col` — 输出参数，接收光标所在字符单元格。
- **返回值**：成功返回 `0`，失败返回 `< 0`。
- **详细说明**：封装 `IO_CURSOR_GET`。注意：QEMU 下模拟 VGA
  文本光标回读可能不可靠——使用时请做防御性校验。

## `int con_cursor_set(uint16_t row, uint16_t col)`

- **参数**：`row`、`col` — 目标单元格。
- **返回值**：成功返回 `0`，失败返回 `< 0`。
- **详细说明**：封装 `IO_CURSOR_SET`；坐标是字符单元格，不是
  像素。

## `int con_size(uint16_t *rows, uint16_t *cols)`

- **参数**：`rows`、`cols` — 输出参数，接收控制台尺寸（字符
  单元格数）。
- **返回值**：成功返回 `0`，失败返回 `< 0`。
- **详细说明**：封装 `IO_CONSOLE_INFO`。
