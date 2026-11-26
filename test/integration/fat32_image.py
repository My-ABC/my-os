import shutil
import subprocess
from pathlib import Path

from common import IntegrationFailure, ROOT


def add_root_file(image_path, filename, contents):
    image = bytearray(Path(image_path).read_bytes())
    bytes_per_sector = int.from_bytes(image[11:13], "little")
    sectors_per_cluster = image[13]
    reserved_sectors = int.from_bytes(image[14:16], "little")
    fat_count = image[16]
    sectors_per_fat = int.from_bytes(image[36:40], "little")
    total_sectors = int.from_bytes(image[32:36], "little")
    root_cluster = int.from_bytes(image[44:48], "little") & 0x0FFFFFFF

    if bytes_per_sector != 512 or sectors_per_cluster == 0 or fat_count == 0:
        raise IntegrationFailure("mkfs.fat produced an unsupported FAT32 image")

    cluster_size = bytes_per_sector * sectors_per_cluster
    data_start_sector = reserved_sectors + fat_count * sectors_per_fat
    data_cluster_count = (total_sectors - data_start_sector) // sectors_per_cluster
    fat_start = reserved_sectors * bytes_per_sector
    data_start = data_start_sector * bytes_per_sector
    fat_size = sectors_per_fat * bytes_per_sector
    cluster_count = max(1, (len(contents) + cluster_size - 1) // cluster_size)

    try:
        base_name, extension = filename.upper().split(".", 1)
        short_name = base_name.encode("ascii").ljust(8, b" ") + \
            extension.encode("ascii").ljust(3, b" ")
    except (UnicodeEncodeError, ValueError) as error:
        raise IntegrationFailure(f"invalid 8.3 test filename: {filename}") from error
    if len(base_name) > 8 or len(extension) > 3 or len(short_name) != 11:
        raise IntegrationFailure(f"test filename does not fit FAT 8.3: {filename}")

    def fat_value(cluster):
        offset = fat_start + cluster * 4
        return int.from_bytes(image[offset:offset + 4], "little") & 0x0FFFFFFF

    free_clusters = [
        cluster for cluster in range(2, data_cluster_count + 2)
        if fat_value(cluster) == 0
    ][:cluster_count]
    if len(free_clusters) != cluster_count:
        raise IntegrationFailure("no free clusters for FAT32 test executable")

    for index, cluster in enumerate(free_clusters):
        next_cluster = (
            free_clusters[index + 1]
            if index + 1 < len(free_clusters) else 0x0FFFFFFF
        )
        for fat_index in range(fat_count):
            offset = fat_start + fat_index * fat_size + cluster * 4
            image[offset:offset + 4] = next_cluster.to_bytes(4, "little")
        data_offset = data_start + (cluster - 2) * cluster_size
        chunk = contents[index * cluster_size:(index + 1) * cluster_size]
        image[data_offset:data_offset + cluster_size] = chunk.ljust(cluster_size, b"\0")

    cluster = root_cluster
    slot_offset = None
    for _ in range(data_cluster_count):
        directory_offset = data_start + (cluster - 2) * cluster_size
        for offset in range(0, cluster_size, 32):
            if image[directory_offset + offset] in (0, 0xE5):
                slot_offset = directory_offset + offset
                break
        if slot_offset is not None:
            break
        cluster = fat_value(cluster)
        if cluster < 2 or cluster >= 0x0FFFFFF8:
            break
    if slot_offset is None:
        raise IntegrationFailure("no free root directory slot for FAT32 test executable")

    entry = bytearray(32)
    entry[:11] = short_name
    entry[11] = 0x20
    first_cluster = free_clusters[0]
    entry[20:22] = (first_cluster >> 16).to_bytes(2, "little")
    entry[26:28] = (first_cluster & 0xFFFF).to_bytes(2, "little")
    entry[28:32] = len(contents).to_bytes(4, "little")
    image[slot_offset:slot_offset + 32] = entry
    Path(image_path).write_bytes(image)


def root_file_present(image_path, filename):
    """True when the FAT32 root directory holds a live (not 0xE5-deleted)
    entry for `filename`. Used to detect disk images whose shell3.elf
    directory entry was removed after the image was created — a plain
    timestamp check cannot see that."""
    image = Path(image_path).read_bytes()
    bytes_per_sector = int.from_bytes(image[11:13], "little")
    sectors_per_cluster = image[13]
    reserved_sectors = int.from_bytes(image[14:16], "little")
    fat_count = image[16]
    sectors_per_fat = int.from_bytes(image[36:40], "little")
    total_sectors = int.from_bytes(image[32:36], "little")
    if total_sectors == 0:
        total_sectors = int.from_bytes(image[19:21], "little") * bytes_per_sector
    root_cluster = int.from_bytes(image[44:48], "little") & 0x0FFFFFFF
    if bytes_per_sector != 512 or sectors_per_cluster == 0:
        return False

    cluster_size = bytes_per_sector * sectors_per_cluster
    data_start = (reserved_sectors + fat_count * sectors_per_fat) * bytes_per_sector
    fat_start = reserved_sectors * bytes_per_sector
    fat_size = sectors_per_fat * bytes_per_sector
    data_cluster_count = max(1, (total_sectors * bytes_per_sector -
                                 data_start) // cluster_size)

    def fat_value(cluster):
        offset = fat_start + cluster * 4
        return int.from_bytes(image[offset:offset + 4], "little") & 0x0FFFFFFF

    try:
        base_name, extension = filename.upper().split(".", 1)
        short_name = base_name.encode("ascii").ljust(8, b" ") + \
            extension.encode("ascii").ljust(3, b" ")
    except (UnicodeEncodeError, ValueError):
        return False

    cluster = root_cluster
    for _ in range(data_cluster_count):
        if cluster < 2 or cluster >= 0x0FFFFFF8:
            return False
        directory_offset = data_start + (cluster - 2) * cluster_size
        directory = image[directory_offset:directory_offset + cluster_size]
        for offset in range(0, len(directory), 32):
            entry = directory[offset:offset + 32]
            if len(entry) < 32:
                break
            if entry[0] == 0x00:
                return False
            if entry[0] == 0xE5 or entry[11] == 0x0F:
                continue
            if bytes(entry[:11]) == short_name:
                return True
        cluster = fat_value(cluster)
    return False


def create_empty_disk(directory):
    """FAT32 image with no files at all — used by the vt spawn-exhaustion
    scenario where /shell3.elf must be absent."""
    disk_path = Path(directory) / "disk.img"
    with disk_path.open("wb") as disk:
        disk.truncate(64 * 1024 * 1024)
    mkfs = shutil.which("mkfs.fat")
    if mkfs is None:
        raise IntegrationFailure("mkfs.fat is required for FAT32 QEMU integration")
    subprocess.run(
        [mkfs, "-F", "32", "-n", "MYOS", str(disk_path)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=True,
    )
    return disk_path


def create_test_disk(directory):
    disk_path = Path(directory) / "disk.img"
    with disk_path.open("wb") as disk:
        disk.truncate(64 * 1024 * 1024)

    mkfs = shutil.which("mkfs.fat")
    if mkfs is None:
        raise IntegrationFailure("mkfs.fat is required for FAT32 QEMU integration")
    subprocess.run(
        [mkfs, "-F", "32", "-n", "MYOS", str(disk_path)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=True,
    )

    nasm = shutil.which("nasm")
    if nasm is None:
        raise IntegrationFailure("NASM is required to assemble the FAT32 ELF demo")
    demo_path = Path(directory) / "demo.elf"
    subprocess.run(
        [nasm, "-f", "bin", str(ROOT / "boot" / "elf_demo.asm"),
         "-o", str(demo_path)],
        cwd=ROOT,
        check=True,
    )
    add_root_file(disk_path, "demo.elf", demo_path.read_bytes())

    fs_demo_path = Path(directory) / "fsdemo.elf"
    subprocess.run(
        [nasm, "-f", "bin", str(ROOT / "test" / "integration" / "fs_demo.asm"),
         "-o", str(fs_demo_path)],
        cwd=ROOT,
        check=True,
    )
    add_root_file(disk_path, "fsdemo.elf", fs_demo_path.read_bytes())

    sig_demo_path = Path(directory) / "sigdemo.elf"
    subprocess.run(
        [nasm, "-f", "bin", str(ROOT / "test" / "integration" / "sig_demo.asm"),
         "-o", str(sig_demo_path)],
        cwd=ROOT,
        check=True,
    )
    add_root_file(disk_path, "sigdemo.elf", sig_demo_path.read_bytes())

    for mode, filename in ((0, "stack.elf"), (1, "faultu.elf"),
                           (2, "faultp.elf"), (3, "oom.elf"),
                           (4, "kfault.elf")):
        fault_path = Path(directory) / filename
        subprocess.run(
            [nasm, "-f", "bin", f"-DFAULT_MODE={mode}",
             str(ROOT / "test" / "integration" / "fault_demo.asm"),
             "-o", str(fault_path)],
            cwd=ROOT,
            check=True,
        )
        add_root_file(disk_path, filename, fault_path.read_bytes())

    bufcheck_path = Path(directory) / "bufcheck.elf"
    subprocess.run(
        ["nasm", "-f", "bin",
         str(ROOT / "test" / "integration" / "bufcheck_demo.asm"),
         "-o", str(bufcheck_path)],
        cwd=ROOT,
        check=True,
    )
    add_root_file(disk_path, "bufcheck.elf", bufcheck_path.read_bytes())
    return disk_path
