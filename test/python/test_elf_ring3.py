import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def asm_hex_values(source, instruction):
    pattern = rf"^\s*{re.escape(instruction)}\s+(0x[0-9A-Fa-f]+)\s*$"
    return [int(value, 16) for value in re.findall(pattern, source, re.MULTILINE)]


class ElfRing3RegressionTests(unittest.TestCase):
    def test_builtin_elf_uses_address_separate_from_linked_ring3_code(self):
        linker_script = (ROOT / "linker.ld").read_text()
        elf_demo = (ROOT / "boot" / "elf_demo.asm").read_text()
        alignment = re.search(r"\.\s*=\s*ALIGN\((\d+)([KkMm]?)\)", linker_script)
        amount = int(alignment.group(1))
        suffix = alignment.group(2).lower()
        linked_user_base = amount * (1024 ** (2 if suffix == "m" else 1 if suffix == "k" else 0))
        elf_addresses = asm_hex_values(elf_demo, "dd")

        # Header entry, PT_LOAD vaddr/paddr and SYS_WRITE string pointer.
        self.assertNotEqual(linked_user_base, 0x00500000)
        self.assertEqual(
            [elf_addresses[0], elf_addresses[2], elf_addresses[3]],
            [0x00500000, 0x00500000, 0x00500000],
        )
        # Message and handler pointers are label offsets rebased onto the
        # 0x00500000 load address.
        self.assertRegex(
            elf_demo,
            r"mov ebx,\s*hello_msg - elf_demo_code \+ 0x00500000",
        )
        self.assertRegex(
            elf_demo,
            r"mov ecx,\s*bye_handler - elf_demo_code \+ 0x00500000",
        )

    def test_elf_run_uses_foreground_scheduler_and_tracks_image_for_cleanup(self):
        elf_source = (ROOT / "src" / "os" / "elf.c").read_text()
        ring3_source = (ROOT / "src" / "os" / "ring3.c").read_text()

        self.assertIn("ring3_schedule_mm", elf_source)
        self.assertNotIn("ring0_to_ring3", elf_source)
        self.assertIn("processes[i].image_start = image_start", ring3_source)
        self.assertIn("processes[i].image_end = image_end", ring3_source)
        self.assertIn("release_process_resources(&processes[i])", ring3_source)
        self.assertIn("if (foreground) foreground_pid = processes[i].pid", ring3_source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
