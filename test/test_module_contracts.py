"""Static contract checks for kernel paths that cannot safely run on the host.

Pure/device routines are also exercised by the C host suites. These checks focus
on privileged entry points, memory layouts, and validation branches where a
host execution would issue privileged instructions or dereference physical
addresses.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text, name):
    """Return one C function body with balanced braces, excluding call sites."""
    signature = re.search(
        r"^[ \t]*(?:static[ \t]+)?[^;{}]*?\b"
        + re.escape(name)
        + r"[ \t]*\([^;{}]*\)[ \t]*\{",
        text,
        re.MULTILINE,
    )
    if not signature:
        raise AssertionError("function definition not found: " + name)
    opening = text.find("{", signature.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError("unterminated function: " + name)


class ModuleContractTests(unittest.TestCase):
    def test_acpi_checksum_rejects_bad_tables(self):
        body = function_body(source("src/acpi.c"), "checksum_ok")
        self.assertIn("sum += data[i]", body)
        self.assertIn("return sum == 0", body)
        self.assertIn("checksum_ok((const uint8_t*)candidate, 20)", source("src/acpi.c"))

    def test_elf_validation_suite_covers_malformed_images(self):
        test = source("test/test_elf.c")
        for case in (
            "rejects null image",
            "rejects truncated header",
            "rejects invalid identification",
            "rejects truncated program-header table",
            "rejects filesz larger than memsz",
            "rejects segment outside image",
            "requires entry in executable segment",
        ):
            with self.subTest(case=case):
                self.assertIn(case, test)

    def test_gdt_descriptor_encoding_and_ring_selectors(self):
        body = function_body(source("src/gdt.c"), "gdt_set_entry")
        for fragment in ("base >> 16", "base >> 24", "limit >> 16",
                         "granularity & 0xF0"):
            self.assertIn(fragment, body)
        header = source("include/gdt.h")
        self.assertIn("#define GDT_TSS_SELECTOR   0x28", header)
        self.assertIn("#define GDT_DF_TSS_SELECTOR 0x30", header)

    def test_heap_rejects_invalid_frees_and_zero_allocations(self):
        text = source("src/heap.c")
        alloc = function_body(text, "kmalloc")
        free = function_body(text, "kfree")
        self.assertIn("requested == 0U", alloc)
        self.assertIn("HEAP_BLOCK_MAGIC", free)
        self.assertIn("block->free != 0U", free)

    def test_idt_task_gate_encoding(self):
        body = function_body(source("src/idt.c"), "idt_set_task_gate")
        self.assertIn("idt[num].base_low = 0", body)
        self.assertIn("idt[num].base_high = 0", body)
        self.assertIn("idt[num].flags = 0x85", body)

    def test_io_widths_are_privileged_and_use_correct_register_constraints(self):
        text = source("src/io.c")
        for mnemonic in ("outb", "inb", "outw", "inw", "outl", "inl"):
            with self.subTest(instruction=mnemonic):
                self.assertIn('"' + mnemonic + " ", text)
        self.assertEqual(text.count('"Nd"(port)'), 6)

    def test_keyboard_scan_set_two_release_prefix_and_bounded_ring(self):
        text = source("src/keyboard.c")
        self.assertIn("scancode == 0xF0", text)
        self.assertIn("(head + 1) % BUFFER_SIZE", text)
        self.assertIn("if (next != tail)", text)
        self.assertIn("current_scancode_set = set", text)

    def test_paging_never_grants_user_flag_to_kernel_address(self):
        text = source("src/paging.c")
        body = function_body(text, "paging_map_page")
        self.assertIn("paging_is_user_address(virtual_addr)", body)
        self.assertIn("(flags & PAGE_USER) != 0U", body)
        self.assertIn("return virtual_addr < USER_SPACE_END", function_body(
            text, "paging_is_user_address"
        ))

    def test_panic_reports_to_serial_and_screen_before_reboot(self):
        body = function_body(source("src/panic.c"), "panic_blue_screen")
        self.assertIn("vga_print(message)", body)
        self.assertIn("serial_print(message)", body)
        self.assertIn("acpi_reboot()", body)

    def test_pic_maps_slave_eoi_and_masks_by_controller(self):
        text = source("src/pic.c")
        self.assertIn("irq < 8 ? PIC1_DATA : PIC2_DATA", text)
        body = function_body(text, "pic_send_eoi")
        self.assertIn("if (irq >= 8)", body)
        self.assertIn("outb(PIC1_COMMAND, PIC_EOI)", body)

    def test_pit_programs_channel_zero_and_exposes_monotonic_tick_updates(self):
        text = source("src/pit.c")
        body = function_body(text, "pit_init")
        self.assertIn("PIT_COMMAND, 0x36", body)
        self.assertIn("PIT_CHANNEL0", body)
        self.assertIn("ticks++", function_body(text, "pit_tick"))

    def test_pmm_free_ignores_out_of_range_and_already_free_frames(self):
        body = function_body(source("src/pmm.c"), "pmm_free_page")
        self.assertIn("if (addr == 0U)", body)
        self.assertIn("frame >= g_pmm.total_frames", body)
        self.assertIn("if (pmm_is_frame_used(frame))", body)

    def test_rtc_suite_covers_epoch_leap_day_and_timezone_rollover(self):
        test = source("test/test_rtc.c")
        for case in ("Unix epoch", "leap-century timestamp", "leap-day timestamp",
                     "positive offset crosses year", "negative offset crosses leap day"):
            with self.subTest(case=case):
                self.assertIn(case, test)

    def test_serial_read_and_line_buffer_are_bounded(self):
        text = source("src/serial.c")
        self.assertIn("if (!serial_is_data_available())", function_body(text, "serial_getchar"))
        body = function_body(text, "serial_gets")
        self.assertIn("while (i < size - 1)", body)
        self.assertIn("buffer[i] = '\\0'", body)

    def test_shell_commands_and_argument_count_sentinel(self):
        text = source("src/shell.c")
        for command in ('{"time"', '{"panic"', '{"ring3"', '{"kill"', '{"elf"'):
            with self.subTest(command=command):
                self.assertIn(command, text)
        body = function_body(text, "split_line")
        self.assertIn("argc < max_args - 1", body)
        self.assertIn("argv[argc] = NULL", body)

    def test_syscall_dispatch_and_user_heap_bounds(self):
        text = source("src/syscall.c")
        self.assertIn("#define USER_HEAP_END   0x40000000U", text)
        self.assertIn("address + pages * PAGE_SIZE < address", text)
        body = function_body(text, "syscall_handler")
        for call in ("SYS_WRITE", "SYS_GETPID", "SYS_MEMORY", "SYS_POWER"):
            self.assertIn("case " + call, body)

    def test_trap_page_fault_uses_fault_address_and_error_context(self):
        text = source("src/trap.c")
        body = function_body(text, "isr14_handler")
        self.assertIn("cr2", body)
        self.assertIn("regs->err_code", body)
        self.assertIn("panic_blue_screen", body)

    def test_irq0_frame_layout_matches_assembly_push_order(self):
        frame = source("include/regs.h")
        assembly = source("boot/interrupt.asm")
        irq0 = assembly.split("irq0:", 1)[1].split("irq1:", 1)[0]
        self.assertRegex(frame, r"struct irq_frame\s*\{\s*uint32_t gs, fs, es, ds;")
        self.assertIn("uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;", frame)
        self.assertIn("uint32_t int_no, err_code;", frame)
        self.assertIn("uint32_t eip, cs, eflags, useresp, ss;", frame)
        for operation in ("push dword 0", "push dword 32", "pusha",
                          "mov ax, ds", "mov ax, es", "mov ax, fs", "mov ax, gs",
                          "call ring3_timer_switch", "popa", "iret"):
            with self.subTest(operation=operation):
                self.assertIn(operation, irq0)
        order = ("push dword 0", "push dword 32", "pusha", "mov ax, ds",
                 "mov ax, es", "mov ax, fs", "mov ax, gs",
                 "call ring3_timer_switch", "popa", "iret")
        offsets = [irq0.index(operation) for operation in order]
        self.assertEqual(offsets, sorted(offsets), "IRQ0 assembly order drifted")

    def test_tss_kernel_stack_update_targets_esp0(self):
        body = function_body(source("src/tss.c"), "tss_set_kernel_stack")
        self.assertIn("tss.esp0 = stack_top", body)

    def test_vbe_rejects_invalid_dimensions_depth_and_color_fields(self):
        body = function_body(source("src/vbe.c"), "vbe_initialize")
        for fragment in ("multiboot_info_addr == 0U", "framebuffer_high != 0U",
                         "vbe_state.width == 0U", "vbe_state.height == 0U",
                         "vbe_state.width > vbe_state.pitch / bytes_per_pixel",
                         "Overlapping framebuffer color fields"):
            with self.subTest(validation=fragment):
                self.assertIn(fragment, body)

    def test_vga_cursor_setter_rejects_out_of_range_coordinates(self):
        body = function_body(source("src/vga.c"), "vga_set_cursor")
        self.assertIn("row < VGA_HEIGHT", body)
        self.assertIn("col < VGA_WIDTH", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
