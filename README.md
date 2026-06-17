# My OS
|[English](README.md)|[中文](README-zh.md)|
A 32-bit x86 operating system kernel built from scratch.
## 🛠️ Build
### Dependency
- `nasm` - Assembler
- `i686-elf-gcc` - Cross compiler
- `i686-elf-ld` - linker
- `qemu-system-i386` - Emulator
- `make` - build tool
### Compile
```bash
make clean && make run
```
### Debugging
```shell
make debug
```
# In another terminal
```shell
gdb-multiarch -ex 'target remote localhost:1234' kernel.elf
```