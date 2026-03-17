#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native panel size before rotation. */
#define ESP_ST7789V2_WIDTH   170
#define ESP_ST7789V2_HEIGHT  320

/* Calibrated RGB565 palette for the validated panel. */
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

/* Display rotations. ROTATION_0 matches the validated hardware preset. */
typedef enum {
    ESP_ST7789V2_ROTATION_0 = 0,
    ESP_ST7789V2_ROTATION_90,
    ESP_ST7789V2_ROTATION_180,
    ESP_ST7789V2_ROTATION_270,
} esp_st7789v2_rotation_t;

/* Horizontal alignment used by text helpers. */
typedef enum {
    ESP_ST7789V2_ALIGN_LEFT = 0,
    ESP_ST7789V2_ALIGN_CENTER,
    ESP_ST7789V2_ALIGN_RIGHT,
} esp_st7789v2_align_t;

/* State container for partial text updates without redrawing the full screen. */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    int scale;
    uint16_t fg;
    uint16_t bg;
    esp_st7789v2_align_t align;
    bool initialized;
    char last_text[64];
} esp_st7789v2_text_box_t;

/* State container for partial progress bar updates. */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    int min;
    int max;
    int value;
    uint16_t border_color;
    uint16_t fill_color;
    uint16_t bg_color;
    bool initialized;
} esp_st7789v2_progress_bar_t;

/* Initializes the display subsystem. Enable AT to register LCD test commands. */
esp_err_t esp_st7789v2_init(bool log_enabled, bool at_enabled);
/* Releases panel, IO and SPI resources allocated by the driver. */
esp_err_t esp_st7789v2_deinit(void);
/* Returns true when the panel is initialized and ready to draw. */
bool esp_st7789v2_is_initialized(void);

/* Applies one of the supported panel rotations. */
esp_err_t esp_st7789v2_set_rotation(esp_st7789v2_rotation_t rotation);
/* Sends INVON/INVOFF to the panel. */
esp_err_t esp_st7789v2_set_invert(bool invert);

/* Draws a single pixel at visible coordinates x,y. */
esp_err_t esp_st7789v2_draw_pixel(int x, int y, uint16_t color);
/* Draws a generic line from x0,y0 to x1,y1. */
esp_err_t esp_st7789v2_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
/* Draws a horizontal line starting at x,y. */
esp_err_t esp_st7789v2_draw_hline(int x, int y, int width, uint16_t color);
/* Draws a vertical line starting at x,y. */
esp_err_t esp_st7789v2_draw_vline(int x, int y, int height, uint16_t color);
/* Draws a rectangular outline. */
esp_err_t esp_st7789v2_draw_rect(int x, int y, int width, int height, uint16_t color);
/* Draws a rounded rectangular outline. Radius is clamped to the box size. */
esp_err_t esp_st7789v2_draw_round_rect(int x, int y, int width, int height, int radius, uint16_t color);
/* Draws a rectangular grid with cols x rows divisions. */
esp_err_t esp_st7789v2_draw_grid(int x, int y, int width, int height, int cols, int rows, uint16_t color);
/* Draws a circle outline centered at cx,cy. */
esp_err_t esp_st7789v2_draw_circle(int cx, int cy, int radius, uint16_t color);
/* Draws a filled circle centered at cx,cy. */
esp_err_t esp_st7789v2_fill_circle(int cx, int cy, int radius, uint16_t color);
/* Draws a triangle outline using three points. */
esp_err_t esp_st7789v2_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
/* Draws a filled rounded rectangle. Radius is clamped to the box size. */
esp_err_t esp_st7789v2_fill_round_rect(int x, int y, int width, int height, int radius, uint16_t color);
/* Fills a rectangular area. */
esp_err_t esp_st7789v2_fill_rect(int x, int y, int width, int height, uint16_t color);
/* Fills the full visible screen. */
esp_err_t esp_st7789v2_fill_screen(uint16_t color);
/* Clears only a specific area with the given background color. */
esp_err_t esp_st7789v2_clear_area(int x, int y, int width, int height, uint16_t bg);
/* Draws one bitmap-font character using the 5x7 font. */
esp_err_t esp_st7789v2_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
/* Draws a string using the 5x7 font. */
esp_err_t esp_st7789v2_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
/* Draws one glyph in the custom 7-segment style. */
esp_err_t esp_st7789v2_draw_7seg_char(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg);
/* Draws a string in the custom 7-segment style. */
esp_err_t esp_st7789v2_draw_7seg_text(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg);
/* Draws a text line aligned against the full screen width. */
esp_err_t esp_st7789v2_draw_text_aligned(int y, const char *text, uint16_t fg, uint16_t bg, int scale, esp_st7789v2_align_t align);
/* Clears a box and redraws text aligned inside that box. */
esp_err_t esp_st7789v2_draw_text_box(int x, int y, int width, int height, const char *text, uint16_t fg, uint16_t bg, int scale, esp_st7789v2_align_t align);
/* Redraws the text box only when the string content changed. */
esp_err_t esp_st7789v2_update_text_box_if_changed(esp_st7789v2_text_box_t *box, const char *text);
/* Draws a horizontal progress bar with border, background and proportional fill. */
esp_err_t esp_st7789v2_draw_progress_bar(int x, int y, int width, int height, int min, int max, int value, uint16_t border_color, uint16_t fill_color, uint16_t bg_color);
/* Redraws the progress bar only when the numeric value changed. */
esp_err_t esp_st7789v2_update_progress_bar_if_changed(esp_st7789v2_progress_bar_t *bar, int value);
/* Returns text width for the 5x7 font and scale. */
int esp_st7789v2_text_width(const char *text, int scale);
/* Returns text width for the 7-segment renderer. */
int esp_st7789v2_7seg_text_width(const char *text, int height, int thickness);

/* Returns current visible width after rotation. */
int esp_st7789v2_width(void);
/* Returns current visible height after rotation. */
int esp_st7789v2_height(void);

#ifdef __cplusplus
}
#endif
