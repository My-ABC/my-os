# 09 — Filesystem (FAT32 on ATA)

English | [中文](../zh/09-filesystem.md)

## ATA layer (`src/drivers/ata.c`)

Primary-master PIO disk access, LBA28 only. `ata_init()` probes the drive;
`ata_read_sectors`/`ata_write_sectors` move 512-byte sectors. No DMA, no
ATAPI, no partition-table parsing — the FAT32 volume starts at a fixed
location expected by `fat32_init()`.

## FAT32 module layout (`src/fs/`)

The implementation is split by concern; shared state and cross-file
prototypes live in the private `fat32_int.h` (`g_fs`, `g_sector`,
`g_cluster`, `struct prepared_name`, `struct dir_entry_location`):

| File | Responsibility |
|---|---|
| `fat32_core.c` | Mount state, sector/cluster I/O, FAT chain traversal, cluster alloc/free, `fat32_init`/`fat32_mounted` |
| `fat32_name.c` | UTF-16LE↔ASCII, 8.3 formatting/validation, LFN checksum/sequence/decode, `prepare_name`, `~N` aliases |
| `fat32_dir.c` | Directory entries, path resolution, listing, slot allocation, insert/update/delete, `mkdir`/`rmdir`, bounded recursive delete, `..` maintenance |
| `fat32.c` | Public API: `fat32_read_file`, `fat32_write_file`, `fat32_copy_file`, `fat32_move`, `fat32_delete_file`, `fat32_make_directory`, `fat32_is_directory`, `fat32_path_within` |

Public declarations: `include/fs/fat32.h`. `FAT32_MAX_FILE_SIZE` =
1 MiB bounds whole-file reads/writes/copies.

`fat32_init` accepts volumes with 1 or 2 FATs and falls back to the
16-bit Total Sectors BPB field (offset 19) when the 32-bit field is
zero — the same fallback `fat32_image.py` applies.

## Name semantics

- Every name written through the public API gets LFN entries preserving
  the original case (vfat-style), plus an 8.3 alias when needed
  (`needs_alias` → generated `~N` name).
- Lookup: entries with a valid LFN match by exact case-sensitive
  `strcmp`; legacy entries without LFN fall back to uppercase 8.3
  matching, so pre-LFN images (e.g. `demo.elf`) keep working.
- LFN names support up to 255 UTF-16 code units; only ASCII converts
  cleanly (`utf16LE_to_ascii`).

## Directory and file semantics

- `fat32_list_directory` iterates a **shared static cluster buffer** —
  callbacks must not perform filesystem I/O. Recursive consumers
  (`cp -r` in the shell) collect entries into a heap array first.
- `fat32_move` = rename/move for files and directories: inserts a new
  directory entry preserving cluster/attributes/timestamps/size, updates
  a moved directory's `..`, refuses to replace an existing directory or
  move a directory into its own subtree, and rolls back on failure.
- `cp`/`mv` destination rule (Linux-like, implemented in the shell):
  if `dst` is an existing directory the source lands inside it as
  `dst/<basename>`; `mv` replaces existing files.
- Recursive delete (`rmdir -f` / `fat32_remove_directory_recursive`) is
  bounded by `FAT32_MAX_DELETE_DEPTH` = 32.

## Known limits

- Single ATA device, PIO only, no caching layer yet (block cache is a
  TODO item).
- No open file handles / offsets — reads and writes are whole-file.
- No journaling; corrupt-disk handling is best-effort.
