#include "vbe.h"
#include "ascii_font.h"
#include "paging.h"
#include "serial.h"

#define VBE_FRAMEBUFFER_ADDRESS 0xE0000000U

typedef uint16_t vbe_unaligned_u16_t __attribute__((aligned(1)));
typedef uint32_t vbe_unaligned_u32_t __attribute__((aligned(1)));

struct vbe_state {
    uint32_t framebuffer;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
    uint8_t valid;
};

static struct vbe_state vbe_state;
static uint32_t vbe_foreground = 0x00FFFFFFU;
static uint32_t vbe_background = 0x00000000U;
static uint32_t vbe_cursor_x;
static uint32_t vbe_cursor_y;
static uint32_t vbe_cursor_blink_ticks;
static uint8_t vbe_cursor_visible = 1;

static uint32_t vbe_palette_color(uint8_t color) {
    static const uint32_t palette[16] = {
        0x00000000U, 0x000000AAU, 0x0000AA00U, 0x0000AAAAU,
        0x00AA0000U, 0x00AA00AAU, 0x00AA5500U, 0x00AAAAAAU,
        0x00555555U, 0x005555FFU, 0x0055FF55U, 0x0055FFFFU,
        0x00FF5555U, 0x00FF55FFU, 0x00FFFF55U, 0x00FFFFFFU
    };
    return palette[color & 0x0FU];
}

static uint32_t vbe_encode_component(uint8_t component, uint8_t position,
                                     uint8_t mask_size) {
    uint32_t maximum = (1U << mask_size) - 1U;
    uint32_t value = ((uint32_t)component * maximum + 127U) / 255U;
    return value << position;
}

static uint32_t vbe_encode_color(uint32_t color) {
    return vbe_encode_component((uint8_t)(color >> 16),
                                vbe_state.red_position,
                                vbe_state.red_mask_size) |
           vbe_encode_component((uint8_t)(color >> 8),
                                vbe_state.green_position,
                                vbe_state.green_mask_size) |
           vbe_encode_component((uint8_t)color,
                                vbe_state.blue_position,
                                vbe_state.blue_mask_size);
}

static void vbe_store_pixel(volatile uint8_t *pixel, uint32_t color) {
    uint32_t encoded = vbe_encode_color(color);

    if (vbe_state.bpp == 8) {
        pixel[0] = (uint8_t)encoded;
    } else if (vbe_state.bpp == 15 || vbe_state.bpp == 16) {
        *(volatile vbe_unaligned_u16_t *)pixel = (uint16_t)encoded;
    } else if (vbe_state.bpp == 24) {
        pixel[0] = (uint8_t)encoded;
        pixel[1] = (uint8_t)(encoded >> 8);
        pixel[2] = (uint8_t)(encoded >> 16);
    } else if (vbe_state.bpp == 32) {
        *(volatile vbe_unaligned_u32_t *)pixel = encoded;
    }
}

static void vbe_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    volatile uint8_t *pixel;
    uint32_t bytes_per_pixel = (vbe_state.bpp + 7U) / 8U;

    /* Callers are expected to keep x < width and y < height; a bad
     * cursor/state value must not turn into a kernel page fault. */
    if (x >= vbe_state.width || y >= vbe_state.height) {
        serial_print("[VBE] dropped out-of-bounds pixel x=");
        serial_print_dec(x);
        serial_print(" y=");
        serial_print_dec(y);
        serial_print(" w=");
        serial_print_dec(vbe_state.width);
        serial_print(" h=");
        serial_print_dec(vbe_state.height);
        serial_print("\n");
        return;
    }
    pixel = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS +
            y * vbe_state.pitch + x * bytes_per_pixel;
    vbe_store_pixel(pixel, color);
}

static void vbe_map_framebuffer(const struct vbe_state *state) {
    uint32_t size = state->pitch * state->height;
    uint32_t pages = (size + 0xFFFU) / 0x1000U;
    for (uint32_t page = 0; page < pages; page++) {
        paging_map_page(VBE_FRAMEBUFFER_ADDRESS + page * 0x1000U,
                        state->framebuffer + page * 0x1000U,
                        PAGE_PRESENT | PAGE_WRITABLE);
    }
}

static void vbe_fill_blue(const struct vbe_state *state) {
    uint32_t bytes_per_pixel = (state->bpp + 7U) / 8U;
    volatile uint8_t *framebuffer = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS;

    for (uint32_t row = 0; row < state->height; row++) {
        volatile uint8_t *line = framebuffer + (uint32_t)row * state->pitch;
        for (uint32_t column = 0; column < state->width; column++) {
            vbe_store_pixel(line + (uint32_t)column * bytes_per_pixel,
                            vbe_palette_color(1U));
        }
    }
}

static void vbe_draw_glyph(const uint8_t *glyph, uint32_t column, uint32_t row) {
    for (uint32_t glyph_row = 0; glyph_row < ASCII_FONT_GLYPH_HEIGHT; glyph_row++) {
        for (uint32_t glyph_bit = 0; glyph_bit < ASCII_FONT_GLYPH_WIDTH; glyph_bit++) {
            uint32_t color = (glyph[glyph_row] & (0x80U >> glyph_bit))
                ? vbe_foreground : vbe_background;
            for (uint32_t scale_y = 0; scale_y < ASCII_FONT_SCALE; scale_y++) {
                for (uint32_t scale_x = 0; scale_x < ASCII_FONT_SCALE; scale_x++) {
                    vbe_put_pixel(column * ASCII_FONT_WIDTH +
                                  glyph_bit * ASCII_FONT_SCALE + scale_x,
                                  row * ASCII_FONT_HEIGHT +
                                  glyph_row * ASCII_FONT_SCALE + scale_y,
                                  color);
                }
            }
        }
    }
}

static void vbe_draw_cursor(void) {
    vbe_cursor_visible = 1;
    for (uint32_t row = ASCII_FONT_HEIGHT - 2; row < ASCII_FONT_HEIGHT; row++) {
        for (uint32_t column = 0; column < ASCII_FONT_WIDTH; column++) {
            vbe_put_pixel(vbe_cursor_x * ASCII_FONT_WIDTH + column,
                          vbe_cursor_y * ASCII_FONT_HEIGHT + row,
                          vbe_foreground);
        }
    }
}

static void vbe_erase_cursor(void) {
    vbe_cursor_visible = 0;
    for (uint32_t row = ASCII_FONT_HEIGHT - 2; row < ASCII_FONT_HEIGHT; row++) {
        for (uint32_t column = 0; column < ASCII_FONT_WIDTH; column++) {
            vbe_put_pixel(vbe_cursor_x * ASCII_FONT_WIDTH + column,
                          vbe_cursor_y * ASCII_FONT_HEIGHT + row,
                          vbe_background);
        }
    }
}

static void vbe_scroll_up(void) {
    uint32_t columns = vbe_state.width / ASCII_FONT_WIDTH;
    uint32_t rows = vbe_state.height / ASCII_FONT_HEIGHT;
    uint32_t scroll_area_rows = rows - 1U;

    for (uint32_t row = 0; row < scroll_area_rows; row++) {
        for (uint32_t column = 0; column < columns; column++) {
            uint32_t src_y = (row + 1U) * ASCII_FONT_HEIGHT;
            uint32_t dst_y = row * ASCII_FONT_HEIGHT;
            for (uint32_t glyph_y = 0; glyph_y < ASCII_FONT_HEIGHT; glyph_y++) {
                for (uint32_t glyph_x = 0; glyph_x < ASCII_FONT_WIDTH; glyph_x++) {
                    uint32_t src_x = column * ASCII_FONT_WIDTH + glyph_x;
                    uint32_t src_color = 0U;
                    volatile uint8_t *src = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS +
                        (src_y + glyph_y) * vbe_state.pitch + src_x * ((vbe_state.bpp + 7U) / 8U);
                    volatile uint8_t *dst = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS +
                        (dst_y + glyph_y) * vbe_state.pitch + src_x * ((vbe_state.bpp + 7U) / 8U);
                    if (vbe_state.bpp == 32) {
                        src_color = *(volatile vbe_unaligned_u32_t *)src;
                        *(volatile vbe_unaligned_u32_t *)dst = src_color;
                    } else {
                        uint32_t bytes_per_pixel = (vbe_state.bpp + 7U) / 8U;
                        for (uint32_t b = 0; b < bytes_per_pixel; b++) {
                            dst[b] = src[b];
                        }
                    }
                }
            }
        }
    }

    for (uint32_t row = scroll_area_rows; row < rows; row++) {
        for (uint32_t column = 0; column < columns; column++) {
            uint32_t start_x = column * ASCII_FONT_WIDTH;
            uint32_t start_y = row * ASCII_FONT_HEIGHT;
            for (uint32_t glyph_y = 0; glyph_y < ASCII_FONT_HEIGHT; glyph_y++) {
                for (uint32_t glyph_x = 0; glyph_x < ASCII_FONT_WIDTH; glyph_x++) {
                    vbe_put_pixel(start_x + glyph_x, start_y + glyph_y, vbe_background);
                }
            }
        }
    }

    vbe_cursor_y = rows - 1U;
    vbe_cursor_x = 0;
}

void vbe_cursor_tick(void) {
    if (!vbe_available()) {
        return;
    }
    if (++vbe_cursor_blink_ticks < 50U) {
        return;
    }
    vbe_cursor_blink_ticks = 0;
    if (vbe_cursor_visible) {
        vbe_erase_cursor();
    } else {
        vbe_draw_cursor();
    }
}

int vbe_available(void) {
    return vbe_state.valid != 0;
}

uint32_t vbe_columns(void) {
    if (!vbe_available()) {
        return 0;
    }
    return vbe_state.width / ASCII_FONT_WIDTH;
}

void vbe_get_cursor(uint16_t *row, uint16_t *col) {
    if (!vbe_available()) {
        *row = 0;
        *col = 0;
        return;
    }
    *row = (uint16_t)vbe_cursor_y;
    *col = (uint16_t)vbe_cursor_x;
}

void vbe_set_cursor(uint16_t row, uint16_t col) {
    uint32_t columns = vbe_state.width / ASCII_FONT_WIDTH;
    uint32_t rows = vbe_state.height / ASCII_FONT_HEIGHT;

    if (!vbe_available() || row >= rows || col >= columns) {
        return;
    }
    vbe_erase_cursor();
    vbe_cursor_x = col;
    vbe_cursor_y = row;
    vbe_cursor_blink_ticks = 0;
    vbe_draw_cursor();
}

void vbe_set_color(uint8_t foreground, uint8_t background) {
    vbe_foreground = vbe_palette_color(foreground);
    vbe_background = vbe_palette_color(background);
}

void vbe_clear(void) {
    uint32_t encoded_color;
    volatile uint8_t *framebuffer;

    if (!vbe_available()) {
        return;
    }

    encoded_color = vbe_encode_color(vbe_background);
    framebuffer = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS;
    if (vbe_state.bpp == 32) {
        for (uint32_t y = 0; y < vbe_state.height; y++) {
            volatile vbe_unaligned_u32_t *row =
                (volatile vbe_unaligned_u32_t *)(framebuffer + y * vbe_state.pitch);
            for (uint32_t x = 0; x < vbe_state.width; x++) {
                row[x] = encoded_color;
            }
        }
    } else if (vbe_state.bpp == 15 || vbe_state.bpp == 16) {
        for (uint32_t y = 0; y < vbe_state.height; y++) {
            volatile vbe_unaligned_u16_t *row =
                (volatile vbe_unaligned_u16_t *)(framebuffer + y * vbe_state.pitch);
            for (uint32_t x = 0; x < vbe_state.width; x++) {
                row[x] = (uint16_t)encoded_color;
            }
        }
    } else if (vbe_state.bpp == 8) {
        for (uint32_t y = 0; y < vbe_state.height; y++) {
            volatile uint8_t *row = framebuffer + y * vbe_state.pitch;
            for (uint32_t x = 0; x < vbe_state.width; x++) {
                row[x] = (uint8_t)encoded_color;
            }
        }
    } else if (vbe_state.bpp == 24) {
        for (uint32_t y = 0; y < vbe_state.height; y++) {
            volatile uint8_t *pixel = framebuffer + y * vbe_state.pitch;
            for (uint32_t x = 0; x < vbe_state.width; x++) {
                pixel[0] = (uint8_t)encoded_color;
                pixel[1] = (uint8_t)(encoded_color >> 8);
                pixel[2] = (uint8_t)(encoded_color >> 16);
                pixel += 3U;
            }
        }
    }
    vbe_cursor_x = 0;
    vbe_cursor_y = 0;
    vbe_cursor_blink_ticks = 0;
    vbe_draw_cursor();
}

void vbe_putchar(char c) {
    const uint8_t *glyph;
    uint8_t glyph_index = (uint8_t)c & 0x7FU;
    uint32_t columns = vbe_state.width / ASCII_FONT_WIDTH;
    uint32_t rows = vbe_state.height / ASCII_FONT_HEIGHT;

    if (!vbe_available()) {
        return;
    }
    vbe_erase_cursor();
    if (c == '\n') {
        vbe_cursor_x = 0;
        vbe_cursor_y++;
    } else if (c == '\r') {
        vbe_cursor_x = 0;
    } else if (c == '\b') {
        if (vbe_cursor_x > 0) {
            vbe_cursor_x--;
            vbe_draw_glyph(ascii_font[' '], vbe_cursor_x, vbe_cursor_y);
        }
    } else if (c == '\t') {
        vbe_cursor_x = (vbe_cursor_x + 4U) & ~3U;
    } else {
        glyph = ascii_font[glyph_index];
        if (glyph_index != ' ' && glyph[0] == 0 && glyph[1] == 0 && glyph[2] == 0 &&
            glyph[3] == 0 && glyph[4] == 0 && glyph[5] == 0 &&
            glyph[6] == 0 && glyph[7] == 0) {
            glyph = ascii_font['?'];
        }
        vbe_draw_glyph(glyph, vbe_cursor_x, vbe_cursor_y);
        vbe_cursor_x++;
    }
    if (vbe_cursor_x >= columns) {
        vbe_cursor_x = 0;
        vbe_cursor_y++;
    }
    if (vbe_cursor_y >= rows) {
        vbe_scroll_up();
    } else {
        vbe_draw_cursor();
    }
}

int vbe_initialize(uint32_t multiboot_info_addr) {
    uint32_t *mb_info = (uint32_t *)multiboot_info_addr;
    uint8_t *mb_info_bytes = (uint8_t *)multiboot_info_addr;
    uint32_t flags;
    uint32_t framebuffer_high;
    uint8_t framebuffer_type;
    uint32_t bytes_per_pixel;

    vbe_state.valid = 0;
    if (multiboot_info_addr == 0U) {
        serial_print("[VBE] Missing Multiboot information\n");
        return -1;
    }

    flags = mb_info[0];
    if ((flags & 0x1000U) == 0U) {
        serial_print("[VBE] GRUB did not provide a framebuffer\n");
        return -1;
    }

    vbe_state.framebuffer = mb_info[22];
    framebuffer_high = mb_info[23];
    vbe_state.pitch = mb_info[24];
    vbe_state.width = mb_info[25];
    vbe_state.height = mb_info[26];
    vbe_state.bpp = (uint8_t)mb_info[27];
    framebuffer_type = mb_info_bytes[109];
    if (framebuffer_type == 1U) {
        /* GRUB aligns the color-info union to a 4-byte boundary. */
        vbe_state.red_position = mb_info_bytes[112];
        vbe_state.red_mask_size = mb_info_bytes[113];
        vbe_state.green_position = mb_info_bytes[114];
        vbe_state.green_mask_size = mb_info_bytes[115];
        vbe_state.blue_position = mb_info_bytes[116];
        vbe_state.blue_mask_size = mb_info_bytes[117];
    }

    if (vbe_state.bpp != 8U && vbe_state.bpp != 15U &&
        vbe_state.bpp != 16U && vbe_state.bpp != 24U &&
        vbe_state.bpp != 32U) {
        serial_print("[VBE] Unsupported framebuffer depth\n");
        return -1;
    }

    bytes_per_pixel = (vbe_state.bpp + 7U) / 8U;

    if (framebuffer_high != 0U || vbe_state.framebuffer == 0U ||
        vbe_state.width == 0U || vbe_state.height == 0U ||
        vbe_state.pitch == 0U ||
        framebuffer_type != 1U ||
        vbe_state.width > vbe_state.pitch / bytes_per_pixel ||
        vbe_state.red_mask_size == 0U || vbe_state.red_mask_size > 8U ||
        vbe_state.green_mask_size == 0U || vbe_state.green_mask_size > 8U ||
        vbe_state.blue_mask_size == 0U || vbe_state.blue_mask_size > 8U ||
        vbe_state.red_position + vbe_state.red_mask_size > vbe_state.bpp ||
        vbe_state.green_position + vbe_state.green_mask_size > vbe_state.bpp ||
        vbe_state.blue_position + vbe_state.blue_mask_size > vbe_state.bpp) {
        serial_print("[VBE] Invalid GRUB framebuffer information\n");
        serial_print("[VBE] bpp/type=");
        serial_print_dec(vbe_state.bpp);
        serial_print("/");
        serial_print_dec(framebuffer_type);
        serial_print(" pitch/width/height=");
        serial_print_dec(vbe_state.pitch);
        serial_print("/");
        serial_print_dec(vbe_state.width);
        serial_print("/");
        serial_print_dec(vbe_state.height);
        serial_print(" RGB=");
        serial_print_dec(vbe_state.red_position);
        serial_print(":");
        serial_print_dec(vbe_state.red_mask_size);
        serial_print("/");
        serial_print_dec(vbe_state.green_position);
        serial_print(":");
        serial_print_dec(vbe_state.green_mask_size);
        serial_print("/");
        serial_print_dec(vbe_state.blue_position);
        serial_print(":");
        serial_print_dec(vbe_state.blue_mask_size);
        serial_print("\n");
        return -1;
    }

    {
        uint32_t red_mask =
            ((1U << vbe_state.red_mask_size) - 1U) << vbe_state.red_position;
        uint32_t green_mask =
            ((1U << vbe_state.green_mask_size) - 1U) << vbe_state.green_position;
        uint32_t blue_mask =
            ((1U << vbe_state.blue_mask_size) - 1U) << vbe_state.blue_position;
        if ((red_mask & green_mask) != 0U || (red_mask & blue_mask) != 0U ||
            (green_mask & blue_mask) != 0U) {
            serial_print("[VBE] Overlapping framebuffer color fields\n");
            return -1;
        }
    }

    vbe_state.valid = 1;
    vbe_map_framebuffer(&vbe_state);

    serial_print("[VBE] GRUB framebuffer=0x");
    serial_print_hex(vbe_state.framebuffer);
    serial_print("\n");
    serial_print("[VBE] RGB fields R=");
    serial_print_dec(vbe_state.red_position);
    serial_print(":");
    serial_print_dec(vbe_state.red_mask_size);
    serial_print(" G=");
    serial_print_dec(vbe_state.green_position);
    serial_print(":");
    serial_print_dec(vbe_state.green_mask_size);
    serial_print(" B=");
    serial_print_dec(vbe_state.blue_position);
    serial_print(":");
    serial_print_dec(vbe_state.blue_mask_size);
    serial_print("\n");
    return 0;
}

int vbe_blue_screen(void) {
    if (!vbe_state.valid) {
        return -1;
    }
    vbe_fill_blue(&vbe_state);
    return 0;
}