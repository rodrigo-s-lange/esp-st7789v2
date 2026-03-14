#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"

#include "esp_st7789v2.h"

#define TAG "esp_st7789v2"
#define FILL_CHUNK_PIXELS 128U

enum {
    SEG_A = 1 << 0,
    SEG_B = 1 << 1,
    SEG_C = 1 << 2,
    SEG_D = 1 << 3,
    SEG_E = 1 << 4,
    SEG_F = 1 << 5,
    SEG_G = 1 << 6,
};

struct esp_st7789v2 {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    spi_host_device_t host;
    gpio_num_t pin_backlight;
    bool backlight_active_high;
    bool invert_color;
    uint16_t width;
    uint16_t height;
    uint16_t x_gap;
    uint16_t y_gap;
    esp_st7789v2_rotation_t rotation;
    bool bus_initialized;
};

static void _apply_rotation_preset(esp_st7789v2_t *display)
{
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    int x_gap = display->x_gap;
    int y_gap = display->y_gap;

    switch (display->rotation) {
        case ESP_ST7789V2_ROTATION_0:
            swap_xy = false;
            mirror_x = true;
            mirror_y = true;
            x_gap = display->x_gap;
            y_gap = display->y_gap;
            break;
        case ESP_ST7789V2_ROTATION_90:
            swap_xy = true;
            mirror_x = true;
            mirror_y = false;
            x_gap = display->y_gap;
            y_gap = display->x_gap;
            break;
        case ESP_ST7789V2_ROTATION_180:
            swap_xy = false;
            mirror_x = false;
            mirror_y = false;
            x_gap = display->x_gap;
            y_gap = display->y_gap;
            break;
        case ESP_ST7789V2_ROTATION_270:
            swap_xy = true;
            mirror_x = false;
            mirror_y = true;
            x_gap = display->y_gap;
            y_gap = display->x_gap;
            break;
    }

    esp_lcd_panel_swap_xy(display->panel, swap_xy);
    esp_lcd_panel_mirror(display->panel, mirror_x, mirror_y);
    esp_lcd_panel_set_gap(display->panel, x_gap, y_gap);
}

static esp_err_t _validate_area(const esp_st7789v2_t *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (display == NULL || w == 0U || h == 0U) return ESP_ERR_INVALID_ARG;
    if (x >= display->width || y >= display->height) return ESP_ERR_INVALID_ARG;
    if ((uint32_t)x + w > display->width || (uint32_t)y + h > display->height) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static uint16_t _7seg_char_width(uint16_t height)
{
    uint16_t w = height / 2U;
    return (w < 8U) ? 8U : w;
}

static uint16_t _7seg_char_advance(uint16_t height, uint16_t thickness)
{
    return (uint16_t)(_7seg_char_width(height) + thickness + 2U);
}

static uint8_t _7seg_mask_for_char(char ch)
{
    switch (ch) {
        case '0': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
        case '1': return SEG_B | SEG_C;
        case '2': return SEG_A | SEG_B | SEG_D | SEG_E | SEG_G;
        case '3': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_G;
        case '4': return SEG_B | SEG_C | SEG_F | SEG_G;
        case '5': return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
        case '6': return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
        case '7': return SEG_A | SEG_B | SEG_C;
        case '8': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
        case '9': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;
        case '-': return SEG_G;
        default: return 0U;
    }
}

static esp_err_t _fill_if_visible(esp_st7789v2_t *display,
                                  uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  uint16_t color)
{
    if (w == 0U || h == 0U) return ESP_OK;
    if (x >= display->width || y >= display->height) return ESP_OK;
    if ((uint32_t)x + w > display->width) {
        w = (uint16_t)(display->width - x);
    }
    if ((uint32_t)y + h > display->height) {
        h = (uint16_t)(display->height - y);
    }
    return esp_st7789v2_fill_rect(display, x, y, w, h, color);
}

static esp_err_t _draw_slash(esp_st7789v2_t *display,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t thickness,
                             uint16_t color)
{
    for (uint16_t i = 0; i < h; i++) {
        uint32_t pos = ((uint32_t)(h - 1U - i) * w) / h;
        uint16_t px = (uint16_t)(x + pos);
        esp_err_t err = _fill_if_visible(display, px, (uint16_t)(y + i), thickness, 1U, color);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t _draw_degree(esp_st7789v2_t *display,
                              uint16_t x,
                              uint16_t y,
                              uint16_t height,
                              uint16_t thickness,
                              uint16_t color_on,
                              uint16_t color_off)
{
    uint16_t size = height / 3U;
    if (size < (uint16_t)(thickness * 2U + 2U)) {
        size = (uint16_t)(thickness * 2U + 2U);
    }
    uint16_t dx = (uint16_t)(x + _7seg_char_width(height) - size);

    if (color_off != color_on) {
        ESP_RETURN_ON_ERROR(_fill_if_visible(display, dx, y, size, size, color_off), TAG, "degree bg failed");
    }
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, dx, y, size, thickness, color_on), TAG, "degree top failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, dx, (uint16_t)(y + size - thickness), size, thickness, color_on), TAG, "degree bot failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, dx, y, thickness, size, color_on), TAG, "degree left failed");
    return _fill_if_visible(display, (uint16_t)(dx + size - thickness), y, thickness, size, color_on);
}

static esp_err_t _draw_7seg_symbol(esp_st7789v2_t *display,
                                   uint16_t x,
                                   uint16_t y,
                                   char ch,
                                   uint16_t height,
                                   uint16_t thickness,
                                   uint16_t color_on,
                                   uint16_t color_off)
{
    uint16_t w = _7seg_char_width(height);
    uint16_t dot = (thickness < 2U) ? 2U : thickness;
    uint16_t mid_y = (uint16_t)(y + height / 2U - thickness / 2U);
    uint16_t bot_y = (uint16_t)(y + height - dot);

    switch (ch) {
        case ' ':
            if (color_off != color_on) {
                return _fill_if_visible(display, x, y, w, height, color_off);
            }
            return ESP_OK;
        case '.':
            return _fill_if_visible(display, (uint16_t)(x + w - dot), bot_y, dot, dot, color_on);
        case ',':
            ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + w - dot), bot_y, dot, dot, color_on), TAG, "comma dot failed");
            return _fill_if_visible(display, (uint16_t)(x + w - dot - thickness / 2U), (uint16_t)(bot_y + dot), dot, thickness, color_on);
        case ':':
            ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + w / 2U - dot / 2U), (uint16_t)(y + height / 3U - dot / 2U), dot, dot, color_on), TAG, "colon top failed");
            return _fill_if_visible(display, (uint16_t)(x + w / 2U - dot / 2U), (uint16_t)(y + (2U * height) / 3U - dot / 2U), dot, dot, color_on);
        case '/':
            return _draw_slash(display, x, y, w, height, thickness, color_on);
        case '+':
            ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + w / 2U - thickness / 2U), y, thickness, height, color_on), TAG, "plus vert failed");
            return _fill_if_visible(display, x, mid_y, w, thickness, color_on);
        default:
            return _draw_degree(display, x, y, height, thickness, color_on, color_off);
    }
}

esp_err_t esp_st7789v2_init(const esp_st7789v2_config_t *config, esp_st7789v2_t **out_display)
{
    if (config == NULL || out_display == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_OK;

    esp_st7789v2_t *display = calloc(1, sizeof(*display));
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    display->host = config->host;
    display->pin_backlight = config->pin_backlight;
    display->backlight_active_high = config->backlight_active_high;
    display->invert_color = config->invert_color;
    display->width = (config->width != 0U) ? config->width : ESP_ST7789V2_WIDTH_170;
    display->height = (config->height != 0U) ? config->height : ESP_ST7789V2_HEIGHT_320;
    display->x_gap = config->x_gap;
    display->y_gap = config->y_gap;
    display->rotation = ESP_ST7789V2_ROTATION_0;

    spi_bus_config_t buscfg = {
        .sclk_io_num = config->pin_sclk,
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(display->width * 40U * sizeof(uint16_t)),
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(config->host, &buscfg, SPI_DMA_CH_AUTO), fail, TAG, "spi bus init failed");
    display->bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->pin_dc,
        .cs_gpio_num = config->pin_cs,
        .pclk_hz = (config->pixel_clock_hz != 0U) ? config->pixel_clock_hz : ESP_ST7789V2_DEFAULT_PCLK_HZ,
        .lcd_cmd_bits = (config->cmd_bits != 0U) ? config->cmd_bits : 8,
        .lcd_param_bits = (config->param_bits != 0U) ? config->param_bits : 8,
        .spi_mode = 0,
        .trans_queue_depth = (config->queue_depth != 0U) ? config->queue_depth : 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi(config->host, &io_config, &display->io), fail, TAG, "panel io failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->pin_rst,
        .rgb_ele_order = config->rgb_order,
        .bits_per_pixel = 16,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(display->io, &panel_config, &display->panel), fail, TAG, "panel create failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(display->panel), fail, TAG, "panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(display->panel), fail, TAG, "panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_invert_color(display->panel, display->invert_color), fail, TAG, "invert failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), fail, TAG, "display on failed");

    _apply_rotation_preset(display);

    if (display->pin_backlight >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = 1ULL << display->pin_backlight,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&bl_cfg), fail, TAG, "backlight gpio failed");
        ESP_GOTO_ON_ERROR(esp_st7789v2_set_backlight(display, true), fail, TAG, "backlight failed");
    }

    *out_display = display;
    ESP_LOGI(TAG, "initialized %ux%u", display->width, display->height);
    return ESP_OK;

fail:
    (void)esp_st7789v2_deinit(display);
    return ret;
}

esp_err_t esp_st7789v2_deinit(esp_st7789v2_t *display)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;

    if (display->panel != NULL) {
        esp_lcd_panel_del(display->panel);
        display->panel = NULL;
    }
    if (display->io != NULL) {
        esp_lcd_panel_io_del(display->io);
        display->io = NULL;
    }
    if (display->bus_initialized) {
        spi_bus_free(display->host);
        display->bus_initialized = false;
    }

    free(display);
    return ESP_OK;
}

esp_err_t esp_st7789v2_set_rotation(esp_st7789v2_t *display, esp_st7789v2_rotation_t rotation)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;
    display->rotation = rotation;
    _apply_rotation_preset(display);
    return ESP_OK;
}

esp_err_t esp_st7789v2_set_invert(esp_st7789v2_t *display, bool invert)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;
    display->invert_color = invert;
    return esp_lcd_panel_invert_color(display->panel, invert);
}

esp_err_t esp_st7789v2_set_backlight(esp_st7789v2_t *display, bool on)
{
    if (display == NULL || display->pin_backlight < 0) return ESP_ERR_INVALID_ARG;
    int level = on ? (display->backlight_active_high ? 1 : 0) : (display->backlight_active_high ? 0 : 1);
    return gpio_set_level(display->pin_backlight, level);
}

esp_err_t esp_st7789v2_display_on(esp_st7789v2_t *display, bool on)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_disp_on_off(display->panel, on);
}

esp_err_t esp_st7789v2_draw_pixel(esp_st7789v2_t *display, uint16_t x, uint16_t y, uint16_t color)
{
    ESP_RETURN_ON_ERROR(_validate_area(display, x, y, 1, 1), TAG, "invalid area");
    return esp_lcd_panel_draw_bitmap(display->panel, x, y, x + 1, y + 1, &color);
}

esp_err_t esp_st7789v2_fill_rect(esp_st7789v2_t *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ESP_RETURN_ON_ERROR(_validate_area(display, x, y, w, h), TAG, "invalid area");

    uint16_t chunk[FILL_CHUNK_PIXELS];
    for (size_t i = 0; i < FILL_CHUNK_PIXELS; i++) {
        chunk[i] = color;
    }

    for (uint16_t row = 0; row < h; row++) {
        uint16_t remaining = w;
        uint16_t x_pos = x;
        while (remaining > 0U) {
            uint16_t span = (remaining > FILL_CHUNK_PIXELS) ? FILL_CHUNK_PIXELS : remaining;
            esp_err_t err = esp_lcd_panel_draw_bitmap(display->panel, x_pos, y + row, x_pos + span, y + row + 1, chunk);
            if (err != ESP_OK) return err;
            x_pos += span;
            remaining -= span;
        }
    }
    return ESP_OK;
}

esp_err_t esp_st7789v2_fill_screen(esp_st7789v2_t *display, uint16_t color)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;
    return esp_st7789v2_fill_rect(display, 0, 0, display->width, display->height, color);
}

esp_err_t esp_st7789v2_draw_bitmap(esp_st7789v2_t *display,
                                 uint16_t x,
                                 uint16_t y,
                                 uint16_t w,
                                 uint16_t h,
                                 const uint16_t *rgb565)
{
    if (rgb565 == NULL) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(_validate_area(display, x, y, w, h), TAG, "invalid area");
    return esp_lcd_panel_draw_bitmap(display->panel, x, y, x + w, y + h, rgb565);
}

esp_err_t esp_st7789v2_draw_7seg_char(esp_st7789v2_t *display,
                                      uint16_t x,
                                      uint16_t y,
                                      char ch,
                                      uint16_t height,
                                      uint16_t thickness,
                                      uint16_t color_on,
                                      uint16_t color_off)
{
    if (display == NULL || height < 6U || thickness == 0U) return ESP_ERR_INVALID_ARG;

    uint16_t w = _7seg_char_width(height);
    uint16_t vlen = (uint16_t)((height > (3U * thickness)) ? ((height - (3U * thickness)) / 2U) : 1U);
    uint16_t inner_w = (w > (2U * thickness)) ? (uint16_t)(w - (2U * thickness)) : 1U;
    uint16_t y_mid = (uint16_t)(y + thickness + vlen);
    uint8_t mask = _7seg_mask_for_char(ch);

    if (mask == 0U && ch != '.' && ch != ',' && ch != ':' && ch != '/' && ch != '+' && ch != ' ' && ch != '-') {
        if ((unsigned char)ch == 0xB0 || (unsigned char)ch == 0xBA) {
            return _draw_degree(display, x, y, height, thickness, color_on, color_off);
        }
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (mask == 0U && (ch == '.' || ch == ',' || ch == ':' || ch == '/' || ch == '+' || ch == ' ')) {
        return _draw_7seg_symbol(display, x, y, ch, height, thickness, color_on, color_off);
    }

    if (color_off != color_on) {
        ESP_RETURN_ON_ERROR(_fill_if_visible(display, x, y, w, height, color_off), TAG, "7seg bg failed");
    }

    ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + thickness), y, inner_w, thickness, (mask & SEG_A) ? color_on : color_off), TAG, "seg A failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + w - thickness), (uint16_t)(y + thickness), thickness, vlen, (mask & SEG_B) ? color_on : color_off), TAG, "seg B failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + w - thickness), (uint16_t)(y_mid + thickness), thickness, vlen, (mask & SEG_C) ? color_on : color_off), TAG, "seg C failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, (uint16_t)(x + thickness), (uint16_t)(y + height - thickness), inner_w, thickness, (mask & SEG_D) ? color_on : color_off), TAG, "seg D failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, x, (uint16_t)(y_mid + thickness), thickness, vlen, (mask & SEG_E) ? color_on : color_off), TAG, "seg E failed");
    ESP_RETURN_ON_ERROR(_fill_if_visible(display, x, (uint16_t)(y + thickness), thickness, vlen, (mask & SEG_F) ? color_on : color_off), TAG, "seg F failed");
    return _fill_if_visible(display, (uint16_t)(x + thickness), y_mid, inner_w, thickness, (mask & SEG_G) ? color_on : color_off);
}

esp_err_t esp_st7789v2_draw_7seg_text(esp_st7789v2_t *display,
                                      uint16_t x,
                                      uint16_t y,
                                      const char *text,
                                      uint16_t height,
                                      uint16_t thickness,
                                      uint16_t color_on,
                                      uint16_t color_off)
{
    if (display == NULL || text == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t cursor_x = x;
    while (*text != '\0') {
        char ch = *text++;
        if ((unsigned char)ch == 0xC2 &&
            ((unsigned char)*text == 0xB0 || (unsigned char)*text == 0xBA)) {
            ch = *text;
            text++;
        }

        esp_err_t err = esp_st7789v2_draw_7seg_char(display, cursor_x, y, ch, height, thickness, color_on, color_off);
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
        cursor_x = (uint16_t)(cursor_x + _7seg_char_advance(height, thickness));
    }
    return ESP_OK;
}

uint16_t esp_st7789v2_get_width(const esp_st7789v2_t *display)
{
    return (display != NULL) ? display->width : 0U;
}

uint16_t esp_st7789v2_get_height(const esp_st7789v2_t *display)
{
    return (display != NULL) ? display->height : 0U;
}

