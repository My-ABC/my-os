import unittest

from contract_support import function_body, source


def fat32_source():
    parts = ["src/fs/fat32_core.c", "src/fs/fat32_name.c",
             "src/fs/fat32_dir.c", "src/fs/fat32.c", "src/fs/fat32_int.h"]
    return "\n".join(source(part) for part in parts)


class StorageAndShellContractTests(unittest.TestCase):
    def test_shell_commands_and_argument_count_sentinel(self):
        text = source("src/shell/shell.c")
        for command in ('{"time"', '{"panic"', '{"ring3"', '{"kill"', '{"elf"',
                        '{"disk"', '{"ls"', '{"cat"', '{"write"', '{"cp"',
                        '{"mv"', '{"rm"', '{"cd"', '{"mkdir"', '{"rmdir"',
                        '{"run"'):
            with self.subTest(command=command):
                self.assertIn(command, text)
        body = function_body(text, "split_line")
        self.assertIn("argc < max_args - 1", body)
        self.assertIn("argv[argc] = NULL", body)

    def test_shell_cd_normalizes_parent_path_and_prompt_shows_current_directory(self):
        text = source("src/shell/shell.c")
        resolver = function_body(text, "disk_resolve_path")
        self.assertIn("component_length == 2U", resolver)
        self.assertIn("output[output_length - 1U] != '/'", resolver)
        prompt = function_body(text, "start_shell")
        self.assertIn("if (fat32_mounted())", prompt)
        self.assertIn('printf("myos %s > ", disk_cwd)', prompt)
        self.assertIn('printf("myos > ")', prompt)
        self.assertIn('serial_print("[SHELL] CWD ")',
                      function_body(text, "cmd_cd"))

    def test_ls_defaults_to_current_path_and_accepts_explicit_path(self):
        body = function_body(source("src/shell/shell.c"), "cmd_ls")
        self.assertIn("argc > 1 ? argv[1] : disk_cwd", body)
        self.assertIn("fat32_list_directory(path, fat32_print_entry)", body)

    def test_cp_recursive_uses_two_pass_directory_walk(self):
        text = source("src/shell/shell.c")
        body = function_body(text, "cmd_cp")
        self.assertIn('strcmp(argv[1], "-r") == 0', body)
        self.assertIn("cp_recursive_copy(src, dst_final, 0U)", body)
        self.assertIn("disk_resolve_destination(src, dst, dst_final", body)
        self.assertIn("fat32_path_within(src, dst_final)", body)
        self.assertIn("fat32_is_directory", text)
        self.assertIn("fat32_make_directory(dst)", text)
        fat32 = fat32_source()
        self.assertIn("int fat32_is_directory(const char *path)", fat32)

    def test_rmdir_force_option_uses_recursive_directory_removal(self):
        body = function_body(source("src/shell/shell.c"), "cmd_rmdir")
        self.assertIn('strcmp(argv[1], "-f") == 0', body)
        self.assertIn("fat32_remove_directory_recursive(path)", body)
        self.assertIn("disk_path_contains_current(path)", body)

    def test_ata_uses_lba28_primary_master_and_sector_io(self):
        text = source("src/drivers/ata.c")
        for fragment in ("ATA_REG_HDDEVSEL", "0xE0U", "ATA_CMD_IDENTIFY",
                         "ATA_CMD_READ_SECTORS", "ATA_CMD_WRITE_SECTORS",
                         "inw(ATA_REG_DATA)", "outw(ATA_REG_DATA"):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, text)

    def test_run_attaches_only_an_existing_disk_image(self):
        makefile = source("Makefile")
        run_target = makefile.split("\nrun: iso\n", 1)[1].split("\n\n", 1)[0]
        self.assertIn('if test -f "$(DISK)"', run_target)
        self.assertIn("-boot order=d", run_target)
        self.assertIn('-drive file="$(DISK)",format=raw,if=ide', run_target)
        self.assertIn("booting without an IDE disk", run_target)
        disk_target = makefile.split("\ndisk:\n", 1)[1].split("\n\n", 1)[0]
        self.assertIn('truncate -s $(DISK_SIZE) "$(DISK)"', disk_target)
        self.assertIn('mkfs.fat -F 32 -n MYOS "$(DISK)"', disk_target)

    def test_fat32_boot_directory_and_cluster_operations_exist(self):
        text = fat32_source()
        for fragment in ("sectors_per_fat", "root_cluster", "FAT32_END_CLUSTER",
                         "0x0FFFFFF7U", "0x10U", "allocate_cluster",
                         "fat32_write_file", "fat32_remove_directory"):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, text)

    def test_fat32_lfn_names_preserve_case_and_match_exactly(self):
        text = fat32_source()
        body = function_body(text, "find_entry_by_name")
        self.assertIn("strcmp(name, long_name) == 0", body)
        self.assertNotIn("ascii_name_equal", text)
        prepare = function_body(text, "prepare_name")
        self.assertIn("prepared->lfn_entries =", prepare)
        self.assertIn("prepared->needs_alias", prepare)

    def test_fat32_resolves_absolute_directory_components(self):
        text = fat32_source()
        body = function_body(text, "resolve_directory")
        self.assertIn("if (path == 0 || path[0] == '\\0')", body)
        self.assertIn("while (*component == '/')", body)
        self.assertNotIn("path[0] == '/') return g_fs.root_cluster", body)
        listing = function_body(text, "fat32_list_directory")
        self.assertIn("(entry[11] & FAT32_ATTR_VOLUME_ID) != 0U", listing)

    def test_fat32_recursive_directory_removal_is_bounded_and_skips_dot_entries(self):
        text = fat32_source()
        body = function_body(text, "fat32_delete_directory_contents")
        self.assertIn("FAT32_MAX_DELETE_DEPTH", body)
        self.assertIn("entry[0] == '.'", body)
        self.assertIn("fat32_delete_directory_contents(child_cluster, depth + 1U)", body)
        self.assertIn("free_cluster_chain(child_cluster)", body)

    def test_qemu_integration_runs_elf_demo_from_fat32_image(self):
        image = source("test/integration/fat32_image.py")
        scenario = source("test/integration/shell_scenarios.py")
        self.assertIn('ROOT / "boot" / "elf_demo.asm"', image)
        self.assertIn('add_root_file(disk_path, "demo.elf"', image)
        self.assertIn('session.send_line("run demo.elf")', scenario)
        self.assertIn('rb"Hello from ELF32"', scenario)


if __name__ == "__main__":
    unittest.main(verbosity=2)
