# `console.h`

基于 `SYS_IO` 的控制台原语——交互式行编辑的构件
（光标移动、原始按键、清屏）。

`int con_getchar(void)` —— 阻塞读键；返回字节或信号
中止时 `-1`。
`void con_clear(void)` —— 清屏并归位光标。
`int con_cursor_get(uint16_t *row, uint16_t *col)` —— 当前光标。
`int con_cursor_set(uint16_t row, uint16_t col)` —— 移动光标。

注意：QEMU 下模拟 VGA 文本光标回读可能不可靠，
使用时建议做防御性校验。
