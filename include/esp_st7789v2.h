#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ST7789V2_WIDTH   170
#define ESP_ST7789V2_HEIGHT  320

#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xE000
#define GREEN       0x06C0
#define LIME        0x0750
#define BLUE        0x0018
#define YELLOW      0xFEC0
#define CYAN        0x07FF
#define MAGENTA     0xD817
#define ORANGE      0xF400
#define PINK        0xE0F6
#define GOLD        0xF680
#define GRAY        0x738E
#define LIGHT_GRAY  0x9CD3
#define DARK_GRAY   0x528A
#define NAVY        0x0010
#define DEEP_BLUE   0x0014
#define SKY_BLUE    0x05DF
#define TEAL        0x0451
#define MAROON      0x7800
#define OLIVE       0x7C40

typedef enum {
    ESP_ST7789V2_ROTATION_0 = 0,
    ESP_ST7789V2_ROTATION_90,
    ESP_ST7789V2_ROTATION_180,
    ESP_ST7789V2_ROTATION_270,
} esp_st7789v2_rotation_t;

typedef enum {
    ESP_ST7789V2_ALIGN_LEFT = 0,
    ESP_ST7789V2_ALIGN_CENTER,
    ESP_ST7789V2_ALIGN_RIGHT,
} esp_st7789v2_align_t;

esp_err_t esp_st7789v2_init(bool log_enabled, bool at_enabled);
esp_err_t esp_st7789v2_deinit(void);
bool esp_st7789v2_is_initialized(void);

esp_err_t esp_st7789v2_set_rotation(esp_st7789v2_rotation_t rotation);
esp_err_t esp_st7789v2_set_invert(bool invert);

esp_err_t esp_st7789v2_draw_pixel(int x, int y, uint16_t color);
esp_err_t esp_st7789v2_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
esp_err_t esp_st7789v2_draw_hline(int x, int y, int width, uint16_t color);
esp_err_t esp_st7789v2_draw_vline(int x, int y, int height, uint16_t color);
esp_err_t esp_st7789v2_draw_rect(int x, int y, int width, int height, uint16_t color);
esp_err_t esp_st7789v2_draw_round_rect(int x, int y, int width, int height, int radius, uint16_t color);
esp_err_t esp_st7789v2_draw_grid(int x, int y, int width, int height, int cols, int rows, uint16_t color);
esp_err_t esp_st7789v2_draw_circle(int cx, int cy, int radius, uint16_t color);
esp_err_t esp_st7789v2_fill_circle(int cx, int cy, int radius, uint16_t color);
esp_err_t esp_st7789v2_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
esp_err_t esp_st7789v2_fill_round_rect(int x, int y, int width, int height, int radius, uint16_t color);
esp_err_t esp_st7789v2_fill_rect(int x, int y, int width, int height, uint16_t color);
esp_err_t esp_st7789v2_fill_screen(uint16_t color);
esp_err_t esp_st7789v2_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
esp_err_t esp_st7789v2_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
esp_err_t esp_st7789v2_draw_text_aligned(int y, const char *text, uint16_t fg, uint16_t bg, int scale, esp_st7789v2_align_t align);
int esp_st7789v2_text_width(const char *text, int scale);

int esp_st7789v2_width(void);
int esp_st7789v2_height(void);

#ifdef __cplusplus
}
#endif
