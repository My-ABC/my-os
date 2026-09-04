#include "vbe.h"
#include "ascii_font.h"
#include "paging.h"
#include "serial.h"

#define VBE_FRAMEBUFFER_ADDRESS 0xE0000000U

struct vbe_state {
    uint32_t framebuffer;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
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

static void vbe_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    volatile uint8_t *pixel;
    uint32_t bytes_per_pixel = (vbe_state.bpp + 7U) / 8U;

    pixel = (volatile uint8_t *)VBE_FRAMEBUFFER_ADDRESS +
            y * vbe_state.pitch + x * bytes_per_pixel;
    if (vbe_state.bpp == 8) {
        pixel[0] = (uint8_t)color;
    } else if (vbe_state.bpp == 15 || vbe_state.bpp == 16) {
        pixel[0] = (uint8_t)(((color >> 19) & 0x1FU) | ((color >> 5) & 0xE0U));
        pixel[1] = (uint8_t)((color >> 3) & 0xFFU);
    } else if (vbe_state.bpp == 24) {
        pixel[0] = (uint8_t)color;
        pixel[1] = (uint8_t)(color >> 8);
        pixel[2] = (uint8_t)(color >> 16);
    } else if (vbe_state.bpp == 32) {
        *(volatile uint32_t *)pixel = color;
    }
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
            volatile uint8_t *pixel = line + (uint32_t)column * bytes_per_pixel;
            if (state->bpp == 8) {
                pixel[0] = 1;
            } else if (state->bpp == 15 || state->bpp == 16) {
                pixel[0] = 0x1F;
                pixel[1] = 0x00;
            } else if (state->bpp == 24) {
                pixel[0] = 0xFF;
                pixel[1] = 0x00;
                pixel[2] = 0x00;
            } else if (state->bpp == 32) {
                *(volatile uint32_t *)pixel = 0x000000FFU;
            }
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

void vbe_set_color(uint8_t foreground, uint8_t background) {
    vbe_foreground = vbe_palette_color(foreground);
    vbe_background = vbe_palette_color(background);
}

void vbe_clear(void) {
    if (!vbe_available()) {
        return;
    }
    for (uint32_t y = 0; y < vbe_state.height; y++) {
        for (uint32_t x = 0; x < vbe_state.width; x++) {
            vbe_put_pixel(x, y, vbe_background);
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
    if (vbe_cursor_x >= columns || vbe_cursor_y >= rows) {
        vbe_clear();
    } else {
        vbe_draw_cursor();
    }
}

int vbe_initialize(uint32_t multiboot_info_addr) {
    uint32_t *mb_info = (uint32_t *)multiboot_info_addr;
    uint32_t flags;
    uint32_t framebuffer_high;

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

    if (framebuffer_high != 0U || vbe_state.framebuffer == 0U ||
        vbe_state.width == 0U || vbe_state.height == 0U ||
        vbe_state.pitch == 0U || vbe_state.bpp == 0U) {
        serial_print("[VBE] Invalid GRUB framebuffer information\n");
        return -1;
    }

    vbe_state.valid = 1;
    vbe_map_framebuffer(&vbe_state);

    serial_print("[VBE] GRUB framebuffer=0x");
    serial_print_hex(vbe_state.framebuffer);
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