import unittest

from contract_support import function_body, source


class ArchitectureContractTests(unittest.TestCase):
    def test_gdt_descriptor_encoding_and_ring_selectors(self):
        body = function_body(source("src/arch/x86/gdt.c"), "gdt_set_entry")
        for fragment in ("base >> 16", "base >> 24", "limit >> 16",
                         "granularity & 0xF0"):
            self.assertIn(fragment, body)
        header = source("include/arch/x86/gdt.h")
        self.assertIn("#define GDT_TSS_SELECTOR   0x28", header)
        self.assertIn("#define GDT_DF_TSS_SELECTOR 0x30", header)

    def test_idt_task_gate_encoding(self):
        body = function_body(source("src/arch/x86/idt.c"), "idt_set_task_gate")
        self.assertIn("idt[num].base_low = 0", body)
        self.assertIn("idt[num].base_high = 0", body)
        self.assertIn("idt[num].flags = 0x85", body)

    def test_io_widths_are_privileged_and_use_correct_register_constraints(self):
        text = source("src/arch/x86/io.c")
        for mnemonic in ("outb", "inb", "outw", "inw", "outl", "inl"):
            with self.subTest(instruction=mnemonic):
                self.assertIn('"' + mnemonic + " ", text)
        self.assertEqual(text.count('"Nd"(port)'), 6)

    def test_pic_maps_slave_eoi_and_masks_by_controller(self):
        text = source("src/drivers/pic.c")
        self.assertIn("irq < 8 ? PIC1_DATA : PIC2_DATA", text)
        body = function_body(text, "pic_send_eoi")
        self.assertIn("if (irq >= 8)", body)
        self.assertIn("outb(PIC1_COMMAND, PIC_EOI)", body)

    def test_pit_programs_channel_zero_and_exposes_monotonic_tick_updates(self):
        text = source("src/drivers/pit.c")
        body = function_body(text, "pit_init")
        self.assertIn("PIT_COMMAND, 0x36", body)
        self.assertIn("PIT_CHANNEL0", body)
        self.assertIn("ticks++", function_body(text, "pit_tick"))

    def test_panic_reports_to_serial_and_screen_before_reboot(self):
        body = function_body(source("src/os/panic.c"), "panic_blue_screen")
        self.assertIn("vga_print(message)", body)
        self.assertIn("serial_print(message)", body)
        self.assertIn("acpi_reboot()", body)

    def test_trap_page_fault_uses_fault_address_and_error_context(self):
        body = function_body(source("src/arch/x86/trap.c"), "isr14_handler")
        self.assertIn("cr2", body)
        self.assertIn("regs->err_code", body)
        self.assertIn("panic_blue_screen", body)

    def test_irq0_frame_layout_matches_assembly_push_order(self):
        frame = source("include/arch/x86/regs.h")
        assembly = source("boot/interrupt.asm")
        irq0 = assembly.split("irq0:", 1)[1].split("irq1:", 1)[0]
        syscall = assembly.split("syscall:", 1)[1].split("isr_common_stub:", 1)[0]
        self.assertRegex(frame, r"struct irq_frame\s*\{\s*uint32_t gs, fs, es, ds;")
        self.assertIn("uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;", frame)
        self.assertIn("uint32_t int_no, err_code;", frame)
        self.assertIn("uint32_t eip, cs, eflags, useresp, ss;", frame)
        order = ("push dword 0", "push dword 32", "pusha", "mov ax, ds",
                 "mov ax, es", "mov ax, fs", "mov ax, gs",
                 "call ring3_timer_switch", "popa", "iret")
        for operation in order:
            with self.subTest(operation=operation):
                self.assertIn(operation, irq0)
        offsets = [irq0.index(operation) for operation in order]
        self.assertEqual(offsets, sorted(offsets), "IRQ0 assembly order drifted")
        for operation in ("push dword 0", "push dword 128", "pusha",
                          "push esp", "call syscall_handler",
                          "mov [esp + 44], eax",
                          "cmp dword [syscall_switch_frame], 0",
                          "jmp irq0_resume"):
            with self.subTest(syscall_operation=operation):
                self.assertIn(operation, syscall)

    def test_ring3_yield_saves_syscall_frame_for_later_resume(self):
        header = source("include/os/syscall.h")
        syscall = source("src/os/syscall.c")
        ring3 = source("src/os/ring3.c")
        self.assertIn("#define SYS_YIELD    21", header)
        self.assertIn("case SYS_YIELD:", function_body(syscall, "syscall_handler"))
        yield_body = function_body(ring3, "ring3_yield_current")
        self.assertIn("uint32_t flags = irq_save()", yield_body)
        self.assertIn("(frame->cs & 3U) != 3U", yield_body)
        self.assertIn("process->frame = frame", yield_body)
        self.assertIn("process->state = RING3_PROCESS_READY", yield_body)
        self.assertIn("return kernel_frame", yield_body)

    def test_tss_kernel_stack_update_targets_esp0(self):
        body = function_body(source("src/arch/x86/tss.c"), "tss_set_kernel_stack")
        self.assertIn("tss.esp0 = stack_top", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
