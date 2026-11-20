import unittest

from contract_support import function_body, source


class DeviceContractTests(unittest.TestCase):
    def test_acpi_checksum_rejects_bad_tables(self):
        body = function_body(source("src/drivers/acpi.c"), "checksum_ok")
        self.assertIn("sum += data[i]", body)
        self.assertIn("return sum == 0", body)
        self.assertIn("checksum_ok((const uint8_t*)candidate, 20)",
                      source("src/drivers/acpi.c"))

    def test_keyboard_scan_set_two_release_prefix_and_bounded_ring(self):
        text = source("src/drivers/keyboard.c")
        self.assertIn("scancode == 0xF0", text)
        self.assertIn("(head + 1) % BUFFER_SIZE", text)
        self.assertIn("if (next != tail)", text)
        self.assertIn("current_scancode_set = set", text)

    def test_serial_read_and_line_buffer_are_bounded(self):
        text = source("src/drivers/serial.c")
        self.assertIn("if (!serial_is_data_available())",
                      function_body(text, "serial_getchar"))
        body = function_body(text, "serial_gets")
        self.assertIn("while (i < size - 1)", body)
        self.assertIn("buffer[i] = '\\0'", body)

    def test_rtc_suite_covers_epoch_leap_day_and_timezone_rollover(self):
        test = source("test/c/test_rtc.c")
        for case in ("Unix epoch", "leap-century timestamp", "leap-day timestamp",
                     "positive offset crosses year", "negative offset crosses leap day"):
            with self.subTest(case=case):
                self.assertIn(case, test)

    def test_vbe_rejects_invalid_dimensions_depth_and_color_fields(self):
        body = function_body(source("src/drivers/vbe.c"), "vbe_initialize")
        for fragment in ("multiboot_info_addr == 0U", "framebuffer_high != 0U",
                         "vbe_state.width == 0U", "vbe_state.height == 0U",
                         "vbe_state.width > vbe_state.pitch / bytes_per_pixel",
                         "Overlapping framebuffer color fields"):
            with self.subTest(validation=fragment):
                self.assertIn(fragment, body)

    def test_vga_cursor_setter_rejects_out_of_range_coordinates(self):
        body = function_body(source("src/drivers/vga.c"), "vga_set_cursor")
        self.assertIn("row < VGA_HEIGHT", body)
        self.assertIn("col < VGA_WIDTH", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
