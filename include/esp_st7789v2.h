#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ST7789V2_WIDTH_170    170U
#define ESP_ST7789V2_HEIGHT_320   320U
#define ESP_ST7789V2_DEFAULT_PCLK_HZ  (40 * 1000 * 1000)

typedef struct esp_st7789v2 esp_st7789v2_t;

typedef enum {
    ESP_ST7789V2_ROTATION_0 = 0,
    ESP_ST7789V2_ROTATION_90,
    ESP_ST7789V2_ROTATION_180,
    ESP_ST7789V2_ROTATION_270,
} esp_st7789v2_rotation_t;

typedef struct {
    spi_host_device_t host;
    gpio_num_t pin_sclk;
    gpio_num_t pin_mosi;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;
    gpio_num_t pin_backlight;
    uint32_t pixel_clock_hz;
    uint16_t width;
    uint16_t height;
    uint16_t x_gap;
    uint16_t y_gap;
    uint8_t cmd_bits;
    uint8_t param_bits;
    uint8_t queue_depth;
    lcd_rgb_element_order_t rgb_order;
    bool invert_color;
    bool backlight_active_high;
} esp_st7789v2_config_t;

esp_err_t esp_st7789v2_init(const esp_st7789v2_config_t *config, esp_st7789v2_t **out_display);
esp_err_t esp_st7789v2_deinit(esp_st7789v2_t *display);

esp_err_t esp_st7789v2_set_rotation(esp_st7789v2_t *display, esp_st7789v2_rotation_t rotation);
esp_err_t esp_st7789v2_set_invert(esp_st7789v2_t *display, bool invert);
esp_err_t esp_st7789v2_set_backlight(esp_st7789v2_t *display, bool on);
esp_err_t esp_st7789v2_display_on(esp_st7789v2_t *display, bool on);

esp_err_t esp_st7789v2_draw_pixel(esp_st7789v2_t *display, uint16_t x, uint16_t y, uint16_t color);
esp_err_t esp_st7789v2_fill_rect(esp_st7789v2_t *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t esp_st7789v2_fill_screen(esp_st7789v2_t *display, uint16_t color);
esp_err_t esp_st7789v2_draw_bitmap(esp_st7789v2_t *display,
                                 uint16_t x,
                                 uint16_t y,
                                 uint16_t w,
                                 uint16_t h,
                                 const uint16_t *rgb565);
esp_err_t esp_st7789v2_draw_7seg_char(esp_st7789v2_t *display,
                                      uint16_t x,
                                      uint16_t y,
                                      char ch,
                                      uint16_t height,
                                      uint16_t thickness,
                                      uint16_t color_on,
                                      uint16_t color_off);
esp_err_t esp_st7789v2_draw_7seg_text(esp_st7789v2_t *display,
                                      uint16_t x,
                                      uint16_t y,
                                      const char *text,
                                      uint16_t height,
                                      uint16_t thickness,
                                      uint16_t color_on,
                                      uint16_t color_off);

uint16_t esp_st7789v2_get_width(const esp_st7789v2_t *display);
uint16_t esp_st7789v2_get_height(const esp_st7789v2_t *display);

#ifdef __cplusplus
}
#endif

