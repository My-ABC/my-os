#include "io.h"
#include "keyboard.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "test_support.h"

static uint16_t write_ports[512];
static uint8_t write_values[512];
static unsigned int write_count;
static uint8_t pic_master;
static uint8_t pic_slave;
static uint8_t lsr = 0x20;
static uint8_t input[128];
static unsigned int input_length;
static unsigned int input_position;
static char serial_output[512];
static unsigned int serial_output_length;

void outb(uint16_t port, uint8_t value) {
    if (write_count < 512U) {
        write_ports[write_count] = port;
        write_values[write_count] = value;
        write_count++;
    }
    if (port == 0x21) pic_master = value;
    if (port == 0xA1) pic_slave = value;
    if (port == SERIAL_COM1 && serial_output_length < sizeof(serial_output)) {
        serial_output[serial_output_length++] = (char)value;
    }
}

uint8_t inb(uint16_t port) {
    if (port == 0x21) return pic_master;
    if (port == 0xA1) return pic_slave;
    if (port == SERIAL_COM1 + 5) return lsr;
    if ((port == SERIAL_COM1 || port == KEYBOARD_DATA_PORT) &&
        input_position < input_length) {
        return input[input_position++];
    }
    return 0;
}

static void reset_input(const uint8_t *bytes, unsigned int length) {
    unsigned int i;
    input_length = length;
    input_position = 0;
    for (i = 0; i < length; i++) input[i] = bytes[i];
}

static void test_keyboard_scancodes(void) {
    const uint8_t scans[] = {0x1E, 0x2A, 0x1E, 0xAA, 0xE0, 0x1C};
    keyboard_init();
    reset_input(scans, sizeof(scans));
    keyboard_irq();
    keyboard_irq();
    keyboard_irq();
    keyboard_irq();
    keyboard_irq();
    keyboard_irq();
    check(keyboard_getchar() == 'a', "keyboard: set-1 key maps to lowercase");
    check(keyboard_getchar() == 'A', "keyboard: shift modifies printable key");
    check(keyboard_getchar() == '\n', "keyboard: extended keypad Enter is decoded");
    check(!keyboard_hit(), "keyboard: draining queue clears pending state");
}

static void test_keyboard_queue_bound(void) {
    unsigned int i;
    unsigned int count = 0;
    uint8_t scans[40];
    for (i = 0; i < 40; i++) scans[i] = 0x1E;
    keyboard_init();
    reset_input(scans, 40);
    for (i = 0; i < 40; i++) keyboard_irq();
    while (keyboard_getchar() != 0) count++;
    check(count == 31U, "keyboard: ring buffer drops input instead of overwriting unread data");
}

static void test_pic_masking_and_eoi(void) {
    write_count = 0;
    pic_master = 0;
    pic_slave = 0;
    pic_set_mask(1);
    check(pic_master == 0x02, "PIC: master IRQ mask sets requested bit");
    pic_clear_mask(1);
    check(pic_master == 0, "PIC: master IRQ mask clears requested bit");
    pic_set_mask(10);
    check(pic_slave == 0x04, "PIC: slave IRQ is translated to local bit");
    write_count = 0;
    pic_send_eoi(10);
    check(write_count == 2 && write_ports[0] == 0xA0 &&
          write_values[0] == 0x20 && write_ports[1] == 0x20 &&
          write_values[1] == 0x20, "PIC: slave EOI acknowledges slave then master");
}

static void test_pit_programming_and_ticks(void) {
    write_count = 0;
    pit_init(100);
    check(write_count == 3 && write_ports[0] == 0x43 &&
          write_values[0] == 0x36 && write_ports[1] == 0x40 &&
          write_values[1] == 0x9B && write_ports[2] == 0x40 &&
          write_values[2] == 0x2E, "PIT: 100 Hz programs channel 0 divisor low/high");
    pit_tick();
    pit_tick();
    check(pit_ticks() >= 2U, "PIT: IRQ tick counter advances");
}

static void test_serial_io(void) {
    uint8_t received[] = {'x', 'y', 'z', 'q'};
    uint8_t line_input[] = {'y', 'z', 'q'};
    char line[3];
    serial_output_length = 0;
    serial_putchar('\n');
    check(serial_output_length == 2 && serial_output[0] == '\r' &&
          serial_output[1] == '\n', "serial: newline emits CRLF");

    input_length = input_position = 0;
    lsr = 0;
    check(serial_getchar() == -1, "serial: nonblocking read reports no data");
    lsr = 0x21;
    reset_input(received, sizeof(received));
    check(serial_getchar() == 'x', "serial: nonblocking read returns available byte");

    reset_input(line_input, sizeof(line_input));
    serial_gets(line, sizeof(line));
    check(line[0] == 'y' && line[1] == 'z' && line[2] == '\0',
          "serial: line input remains within caller buffer");
}

int main(void) {
    test_keyboard_scancodes();
    test_keyboard_queue_bound();
    test_pic_masking_and_eoi();
    test_pit_programming_and_ticks();
    test_serial_io();
    return finish_test_suite("Device driver tests");
}
