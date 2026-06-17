# My OS

|[Englist](README.md)|[中文](README-zh.md)|

一个从零开始的 32 位 x86 操作系统内核。

## 🛠️ 构建

### 依赖

- `nasm` - 汇编器
- `i686-elf-gcc` - 交叉编译器
- `i686-elf-ld` - 链接器
- `qemu-system-i386` - 模拟器
- `make` - 构建工具

### 编译

```bash
make clean && make run
```

### 调试
```shell
make debug
```
# 在另一个终端
```shell
gdb-multiarch -ex 'target remote localhost:1234' kernel.elf
```

