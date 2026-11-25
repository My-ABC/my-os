# 09 — 文件系统（ATA 上的 FAT32）

[English](../en/09-filesystem.md) | 中文

## ATA 层（`src/drivers/ata.c`）

主盘 PIO 访问，仅 LBA28。`ata_init()` 探测磁盘；
`ata_read_sectors`/`ata_write_sectors` 读写 512 字节扇区。无 DMA、无
ATAPI、无分区表解析——FAT32 卷从 `fat32_init()` 约定的固定位置开始。

## FAT32 模块划分（`src/fs/`）

实现按职责拆分；共享状态与跨文件原型在私有头 `fat32_int.h`
（`g_fs`、`g_sector`、`g_cluster`、`struct prepared_name`、
`struct dir_entry_location`）：

| 文件 | 职责 |
|---|---|
| `fat32_core.c` | 挂载状态、扇区/簇 I/O、FAT 链遍历、簇分配/释放、`fat32_init`/`fat32_mounted` |
| `fat32_name.c` | UTF-16LE↔ASCII、8.3 格式化/校验、LFN 校验和/序列/解码、`prepare_name`、`~N` 别名 |
| `fat32_dir.c` | 目录项、路径解析、列目录、槽位分配、插入/更新/删除、`mkdir`/`rmdir`、有界递归删除、`..` 维护 |
| `fat32.c` | 公开 API：`fat32_read_file`、`fat32_write_file`、`fat32_copy_file`、`fat32_move`、`fat32_delete_file`、`fat32_make_directory`、`fat32_is_directory`、`fat32_path_within` |

公开声明在 `include/fs/fat32.h`。`FAT32_MAX_FILE_SIZE` = 1 MiB，约束
整个文件的读/写/复制。

## 文件名语义

- 通过公开 API 写入的名字总是生成 LFN 项以保留原始大小写（vfat 风
  格），需要时附带 8.3 别名（`needs_alias` → 生成 `~N` 名）。
- 查找：带有效 LFN 的项按 `strcmp` 精确匹配（区分大小写）；无 LFN 的
  旧条目回退到大写 8.3 匹配，因此 LFN 之前的镜像（如 `demo.elf`）仍
  可用。
- LFN 名支持最多 255 个 UTF-16 码元；只有 ASCII 能干净转换
  （`utf16LE_to_ascii`）。

## 目录与文件语义

- `fat32_list_directory` 遍历**共享的静态簇缓冲**——回调内不能做文件
  系统 I/O。递归消费者（Shell 的 `cp -r`）先把目录项收集到堆数组。
- `fat32_move` = 文件和目录的 rename/move：插入保留簇链/属性/时间戳/
  大小的新目录项，移动目录时更新其 `..`，拒绝替换已存在目录或把目录
  移入自身子树，失败时回滚。
- `cp`/`mv` 目标规则（类 Linux，由 Shell 实现）：`dst` 是已存在目录
  时源放入其中即 `dst/<basename>`；`mv` 替换已存在文件。
- 递归删除（`rmdir -f` / `fat32_remove_directory_recursive`）受
  `FAT32_MAX_DELETE_DEPTH` = 32 限制。

## 已知限制

- 单 ATA 设备、仅 PIO、尚无缓存层（块缓存是 TODO 项）。
- 没有 open 文件句柄/偏移——读写都是整文件。
- 无日志结构；损坏磁盘处理为尽力而为。
