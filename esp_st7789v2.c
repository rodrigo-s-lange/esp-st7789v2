#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_at.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/cdefs.h"

#include "esp_st7789v2.h"

#define LCD_SPI_HOST              SPI2_HOST
#define LCD_PIN_SCLK              5
#define LCD_PIN_MOSI              4
#define LCD_PIN_CS                1
#define LCD_PIN_DC                2
#define LCD_PIN_RST               42
#define LCD_WIDTH_DEFAULT         320
#define LCD_HEIGHT_DEFAULT        170
#define LCD_X_GAP_DEFAULT         0
#define LCD_Y_GAP_DEFAULT         35
#define LCD_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)
#define LCD_CMD_BITS              8
#define LCD_PARAM_BITS            8
#define LCD_BITS_PER_PIXEL        16
#define LCD_FILL_LINES_DEFAULT    1
#define LCD_TRANS_QUEUE_DEPTH     10

typedef struct {
    spi_host_device_t host;
    int pin_sclk;
    int pin_mosi;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int width;
    int height;
    int x_gap;
    int y_gap;
    int pixel_clock_hz;
    uint8_t cmd_bits;
    uint8_t param_bits;
    uint8_t bits_per_pixel;
    bool invert_color;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    lcd_rgb_element_order_t rgb_order;
    size_t fill_lines;
} esp_st7789v2_config_t;

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} st7789_lcd_init_cmd_t;

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const st7789_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} st7789_panel_t;

typedef struct {
    const st7789_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} st7789_vendor_config_t;

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    esp_st7789v2_config_t base_config;
    esp_st7789v2_config_t config;
    esp_st7789v2_rotation_t rotation;
    uint16_t *fill_buffer;
    size_t fill_buffer_pixels;
    bool initialized;
    bool log_enabled;
    bool at_enabled;
} st7789_state_t;

static const char *TAG = "esp_st7789v2";
static st7789_state_t s_state = {0};

static esp_err_t panel_st7789_del(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789_init(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_st7789_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_st7789_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_st7789_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_st7789_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_st7789_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
static esp_err_t esp_st7789v2_new_panel_st7789(const esp_lcd_panel_io_handle_t io,
                                               const esp_lcd_panel_dev_config_t *panel_dev_config,
                                               esp_lcd_panel_handle_t *ret_panel);
static esp_err_t apply_rotation(esp_st7789v2_rotation_t rotation);
static esp_err_t transform_rect(int x, int y, int width, int height, int *out_x, int *out_y);
static inline uint16_t rgb565_swap(uint16_t color);
static inline bool ready(void);
static esp_st7789v2_config_t default_config(void);
static const uint8_t *font5x7_for_char(char c);
static char *trim_ws(char *s);
static bool streq_ignore_case(const char *a, const char *b);
static esp_err_t parse_i32(const char *text, int *out_value);
static esp_err_t parse_u16_color(const char *text, uint16_t *out_color);
static esp_err_t parse_pixel_params(const char *param, int *x, int *y, uint16_t *color);
static esp_err_t parse_rect_params(const char *param, int *a, int *b, int *c, int *d, uint16_t *color);
static esp_err_t parse_round_rect_params(const char *param, int *x, int *y, int *width, int *height, int *radius, uint16_t *color);
static esp_err_t parse_line_params(const char *param, int *x0, int *y0, int *x1, int *y1, uint16_t *color);
static esp_err_t parse_triangle_params(const char *param, int *x0, int *y0, int *x1, int *y1, int *x2, int *y2, uint16_t *color);
static esp_err_t parse_grid_params(const char *param, int *x, int *y, int *width, int *height, int *cols, int *rows, uint16_t *color);
static esp_err_t parse_text_params(const char *param, int *x, int *y, int *scale, uint16_t *fg, uint16_t *bg, const char **text);
static esp_err_t parse_text_box_params(const char *param, int *x, int *y, int *width, int *height, int *scale, uint16_t *fg, uint16_t *bg, esp_st7789v2_align_t *align, const char **text);
static esp_err_t parse_7seg_params(const char *param, int *x, int *y, int *height, int *thickness, uint16_t *fg, uint16_t *bg, const char **text);
static esp_err_t parse_7seg_box_params(const char *param, int *x, int *y, int *width, int *height, int *thickness, uint16_t *fg, uint16_t *bg, esp_st7789v2_align_t *align, const char **text);
static esp_err_t parse_progress_params(const char *param, int *x, int *y, int *width, int *height, int *min, int *max, int *value, uint16_t *border_color, uint16_t *fill_color, uint16_t *bg_color);
static void handle_at_lcdclr(const char *param);
static void handle_at_lcdpx(const char *param);
static void handle_at_lcdline(const char *param);
static void handle_at_lcdhl(const char *param);
static void handle_at_lcdvl(const char *param);
static void handle_at_lcdrect(const char *param);
static void handle_at_lcdrrect(const char *param);
static void handle_at_lcdfill(const char *param);
static void handle_at_lcdfrrect(const char *param);
static void handle_at_lcdgrid(const char *param);
static void handle_at_lcdcirc(const char *param);
static void handle_at_lcdfcirc(const char *param);
static void handle_at_lcdtri(const char *param);
static void handle_at_lcdftri(const char *param);
static void handle_at_lcdtxt(const char *param);
static void handle_at_lcdbox(const char *param);
static void handle_at_lcd7seg(const char *param);
static void handle_at_lcd7box(const char *param);
static void handle_at_lcdbar(const char *param);

#define LCD_LOGI(...)  do { if (s_state.log_enabled) ESP_LOGI(TAG, __VA_ARGS__); } while (0)
#define LCD_LOGW(...)  do { if (s_state.log_enabled) ESP_LOGW(TAG, __VA_ARGS__); } while (0)

static const st7789_lcd_init_cmd_t vendor_specific_init_default[] = {
    {LCD_CMD_SWRESET, NULL, 0, 150},
    {LCD_CMD_SLPOUT, NULL, 0, 10},
    {LCD_CMD_CASET, (uint8_t[]){0x00, 0x00, 0x00, 0xF0}, 4, 0},
    {LCD_CMD_RASET, (uint8_t[]){0x00, 0x00, 0x01, 0x40}, 4, 0},
    {LCD_CMD_INVON, NULL, 0, 10},
    {LCD_CMD_NORON, NULL, 0, 10},
    {LCD_CMD_DISPON, NULL, 0, 10},
};

static esp_st7789v2_config_t default_config(void)
{
    return (esp_st7789v2_config_t) {
        .host = LCD_SPI_HOST,
        .pin_sclk = LCD_PIN_SCLK,
        .pin_mosi = LCD_PIN_MOSI,
        .pin_cs = LCD_PIN_CS,
        .pin_dc = LCD_PIN_DC,
        .pin_rst = LCD_PIN_RST,
        .width = LCD_WIDTH_DEFAULT,
        .height = LCD_HEIGHT_DEFAULT,
        .x_gap = LCD_X_GAP_DEFAULT,
        .y_gap = LCD_Y_GAP_DEFAULT,
        .pixel_clock_hz = LCD_PIXEL_CLOCK_HZ,
        .cmd_bits = LCD_CMD_BITS,
        .param_bits = LCD_PARAM_BITS,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .invert_color = true,
        .swap_xy = true,
        .mirror_x = false,
        .mirror_y = true,
        .rgb_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .fill_lines = LCD_FILL_LINES_DEFAULT,
    };
}

static inline uint16_t rgb565_swap(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static inline bool ready(void)
{
    return s_state.initialized && s_state.panel_handle != NULL;
}

static char *trim_ws(char *s)
{
    if (s == NULL) return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }

    size_t len = strlen(s);
    while (len > 0U) {
        char c = s[len - 1U];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[len - 1U] = '\0';
        len--;
    }

    return s;
}

static bool streq_ignore_case(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0') {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static esp_err_t parse_i32(const char *text, int *out_value)
{
    if (text == NULL || out_value == NULL) return ESP_ERR_INVALID_ARG;

    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    *out_value = (int)value;
    return ESP_OK;
}

static esp_err_t parse_u16_color(const char *text, uint16_t *out_color)
{
    if (text == NULL || out_color == NULL) return ESP_ERR_INVALID_ARG;

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 0xFFFFUL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_color = (uint16_t)value;
    return ESP_OK;
}

static esp_err_t parse_pixel_params(const char *param, int *x, int *y, uint16_t *color)
{
    if (param == NULL || x == NULL || y == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[64];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[3] = {0};
    char *cursor = work;
    for (int i = 0; i < 3; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 2) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 3; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[2], color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t parse_rect_params(const char *param, int *a, int *b, int *c, int *d, uint16_t *color)
{
    if (param == NULL || a == NULL || b == NULL || c == NULL || d == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[96];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[5] = {0};
    char *cursor = work;
    for (int i = 0; i < 5; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 4) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 5; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], a) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], b) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], c) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], d) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[4], color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t parse_round_rect_params(const char *param, int *x, int *y, int *width, int *height, int *radius, uint16_t *color)
{
    if (param == NULL || x == NULL || y == NULL || width == NULL || height == NULL || radius == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[128];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[6] = {0};
    char *cursor = work;
    for (int i = 0; i < 6; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 5) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 6; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], width) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], radius) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[5], color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t parse_line_params(const char *param, int *x0, int *y0, int *x1, int *y1, uint16_t *color)
{
    return parse_rect_params(param, x0, y0, x1, y1, color);
}

static esp_err_t parse_triangle_params(const char *param, int *x0, int *y0, int *x1, int *y1, int *x2, int *y2, uint16_t *color)
{
    if (param == NULL || x0 == NULL || y0 == NULL || x1 == NULL || y1 == NULL || x2 == NULL || y2 == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[128];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[7] = {0};
    char *cursor = work;
    for (int i = 0; i < 7; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 6) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 7; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x0) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y0) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], x1) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], y1) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], x2) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[5], y2) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[6], color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t parse_grid_params(const char *param, int *x, int *y, int *width, int *height, int *cols, int *rows, uint16_t *color)
{
    if (param == NULL || x == NULL || y == NULL || width == NULL || height == NULL || cols == NULL || rows == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[128];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[7] = {0};
    char *cursor = work;
    for (int i = 0; i < 7; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 6) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 7; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], width) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], cols) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[5], rows) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[6], color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t parse_text_params(const char *param, int *x, int *y, int *scale, uint16_t *fg, uint16_t *bg, const char **text)
{
    if (param == NULL || x == NULL || y == NULL || scale == NULL || fg == NULL || bg == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static char work[192];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[6] = {0};
    char *cursor = work;
    for (int i = 0; i < 6; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 5) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        }
    }

    for (int i = 0; i < 6; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], scale) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[3], fg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[4], bg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parts[5] == NULL || *parts[5] == '\0') return ESP_ERR_INVALID_ARG;

    *text = parts[5];
    return ESP_OK;
}

static esp_err_t parse_text_box_params(const char *param, int *x, int *y, int *width, int *height, int *scale, uint16_t *fg, uint16_t *bg, esp_st7789v2_align_t *align, const char **text)
{
    if (param == NULL || x == NULL || y == NULL || width == NULL || height == NULL || scale == NULL || fg == NULL || bg == NULL || align == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static char work[224];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[9] = {0};
    char *cursor = work;
    for (int i = 0; i < 9; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 8) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        }
    }

    for (int i = 0; i < 9; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], width) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], scale) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[5], fg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[6], bg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parts[8] == NULL || *parts[8] == '\0') return ESP_ERR_INVALID_ARG;

    if (streq_ignore_case(parts[7], "LEFT")) {
        *align = ESP_ST7789V2_ALIGN_LEFT;
    } else if (streq_ignore_case(parts[7], "CENTER")) {
        *align = ESP_ST7789V2_ALIGN_CENTER;
    } else if (streq_ignore_case(parts[7], "RIGHT")) {
        *align = ESP_ST7789V2_ALIGN_RIGHT;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    *text = parts[8];
    return ESP_OK;
}

static esp_err_t parse_7seg_params(const char *param, int *x, int *y, int *height, int *thickness, uint16_t *fg, uint16_t *bg, const char **text)
{
    if (param == NULL || x == NULL || y == NULL || height == NULL || thickness == NULL || fg == NULL || bg == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static char work[192];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[7] = {0};
    char *cursor = work;
    for (int i = 0; i < 7; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 6) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        }
    }

    for (int i = 0; i < 7; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], thickness) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[4], fg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[5], bg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parts[6] == NULL || *parts[6] == '\0') return ESP_ERR_INVALID_ARG;

    *text = parts[6];
    return ESP_OK;
}

static esp_err_t parse_7seg_box_params(const char *param, int *x, int *y, int *width, int *height, int *thickness, uint16_t *fg, uint16_t *bg, esp_st7789v2_align_t *align, const char **text)
{
    if (param == NULL || x == NULL || y == NULL || width == NULL || height == NULL || thickness == NULL || fg == NULL || bg == NULL || align == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static char work[224];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[9] = {0};
    char *cursor = work;
    for (int i = 0; i < 9; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 8) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        }
    }

    for (int i = 0; i < 9; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], width) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], thickness) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[5], fg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[6], bg) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parts[8] == NULL || *parts[8] == '\0') return ESP_ERR_INVALID_ARG;

    if (streq_ignore_case(parts[7], "LEFT")) {
        *align = ESP_ST7789V2_ALIGN_LEFT;
    } else if (streq_ignore_case(parts[7], "CENTER")) {
        *align = ESP_ST7789V2_ALIGN_CENTER;
    } else if (streq_ignore_case(parts[7], "RIGHT")) {
        *align = ESP_ST7789V2_ALIGN_RIGHT;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    *text = parts[8];
    return ESP_OK;
}

static esp_err_t parse_progress_params(const char *param, int *x, int *y, int *width, int *height, int *min, int *max, int *value, uint16_t *border_color, uint16_t *fill_color, uint16_t *bg_color)
{
    if (param == NULL || x == NULL || y == NULL || width == NULL || height == NULL || min == NULL || max == NULL || value == NULL || border_color == NULL || fill_color == NULL || bg_color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char work[160];
    strncpy(work, param, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    char *parts[10] = {0};
    char *cursor = work;
    for (int i = 0; i < 10; i++) {
        parts[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 9) {
            if (comma == NULL) return ESP_ERR_INVALID_ARG;
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int i = 0; i < 10; i++) {
        parts[i] = trim_ws(parts[i]);
    }

    if (parse_i32(parts[0], x) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[1], y) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[2], width) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[3], height) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[4], min) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[5], max) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_i32(parts[6], value) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[7], border_color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[8], fill_color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (parse_u16_color(parts[9], bg_color) != ESP_OK) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static const uint8_t *font5x7_for_char(char c)
{
    static const uint8_t glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyph_qmark[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    static const uint8_t glyph_dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const uint8_t glyph_colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const uint8_t glyph_slash[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
    static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t glyph_percent[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};

    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C},
    };

    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    };

    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }

    switch (c) {
        case ' ': return glyph_space;
        case '-': return glyph_dash;
        case '.': return glyph_dot;
        case ':': return glyph_colon;
        case '/': return glyph_slash;
        case '+': return glyph_plus;
        case '%': return glyph_percent;
        default: return glyph_qmark;
    }
}

esp_err_t esp_st7789v2_init(bool log_enabled, bool at_enabled)
{
    if (s_state.initialized) return ESP_ERR_INVALID_STATE;

    s_state.log_enabled = log_enabled;
    s_state.at_enabled = at_enabled;
    s_state.base_config = default_config();
    s_state.config = s_state.base_config;
    s_state.rotation = ESP_ST7789V2_ROTATION_0;

    const esp_st7789v2_config_t *config = &s_state.base_config;

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = config->pin_sclk,
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(config->width * config->fill_lines * sizeof(uint16_t)),
    };

    esp_err_t err = spi_bus_initialize(config->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = config->pin_cs,
        .dc_gpio_num = config->pin_dc,
        .pclk_hz = config->pixel_clock_hz,
        .lcd_cmd_bits = config->cmd_bits,
        .lcd_param_bits = config->param_bits,
        .spi_mode = 0,
        .trans_queue_depth = LCD_TRANS_QUEUE_DEPTH,
    };

    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)config->host, &io_cfg, &s_state.io_handle);
    if (err != ESP_OK) {
        spi_bus_free(config->host);
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = config->pin_rst,
        .rgb_ele_order = config->rgb_order,
        .bits_per_pixel = config->bits_per_pixel,
    };

    err = esp_st7789v2_new_panel_st7789(s_state.io_handle, &panel_cfg, &s_state.panel_handle);
    if (err != ESP_OK) {
        (void)esp_lcd_panel_io_del(s_state.io_handle);
        s_state.io_handle = NULL;
        spi_bus_free(config->host);
        return err;
    }

    s_state.fill_buffer_pixels = (size_t)config->width * config->fill_lines;
    s_state.fill_buffer = heap_caps_malloc(s_state.fill_buffer_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (s_state.fill_buffer == NULL) {
        (void)esp_lcd_panel_del(s_state.panel_handle);
        (void)esp_lcd_panel_io_del(s_state.io_handle);
        (void)spi_bus_free(config->host);
        s_state.panel_handle = NULL;
        s_state.io_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    err = esp_lcd_panel_reset(s_state.panel_handle);
    if (err == ESP_OK) err = esp_lcd_panel_init(s_state.panel_handle);
    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_state.panel_handle, config->invert_color);
    if (err == ESP_OK) err = apply_rotation(ESP_ST7789V2_ROTATION_0);
    if (err == ESP_OK) err = esp_lcd_panel_disp_on_off(s_state.panel_handle, true);
    if (err != ESP_OK) {
        if (s_state.fill_buffer != NULL) {
            heap_caps_free(s_state.fill_buffer);
            s_state.fill_buffer = NULL;
            s_state.fill_buffer_pixels = 0;
        }
        (void)esp_lcd_panel_del(s_state.panel_handle);
        (void)esp_lcd_panel_io_del(s_state.io_handle);
        (void)spi_bus_free(config->host);
        s_state.panel_handle = NULL;
        s_state.io_handle = NULL;
        return err;
    }

    s_state.initialized = true;
    if (s_state.at_enabled) {
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDCLR", handle_at_lcdclr, "AT+LCDCLR=0x0000"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDPX", handle_at_lcdpx, "AT+LCDPX=10,10,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDLINE", handle_at_lcdline, "AT+LCDLINE=0,0,319,169,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDHL", handle_at_lcdhl, "AT+LCDHL=10,20,100,0,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDVL", handle_at_lcdvl, "AT+LCDVL=20,10,0,80,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDRECT", handle_at_lcdrect, "AT+LCDRECT=10,10,80,40,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDRRECT", handle_at_lcdrrect, "AT+LCDRRECT=20,20,120,60,12,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDFILL", handle_at_lcdfill, "AT+LCDFILL=10,10,80,40,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDFRRECT", handle_at_lcdfrrect, "AT+LCDFRRECT=20,20,120,60,12,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDGRID", handle_at_lcdgrid, "AT+LCDGRID=10,10,300,150,10,5,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDCIRC", handle_at_lcdcirc, "AT+LCDCIRC=160,85,40,0,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDFCIRC", handle_at_lcdfcirc, "AT+LCDFCIRC=160,85,40,0,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDTRI", handle_at_lcdtri, "AT+LCDTRI=40,140,160,20,280,140,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDFTRI", handle_at_lcdftri, "AT+LCDFTRI=40,140,160,20,280,140,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDTXT", handle_at_lcdtxt, "AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDBOX", handle_at_lcdbox, "AT+LCDBOX=10,10,120,30,2,0xFFFF,0x0000,CENTER,HELLO"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCD7SEG", handle_at_lcd7seg, "AT+LCD7SEG=10,10,48,8,0xFFFF,0x0000,12:34"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCD7BOX", handle_at_lcd7box, "AT+LCD7BOX=10,10,150,48,8,0xFFFF,0x0000,RIGHT,25°C"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDBAR", handle_at_lcdbar, "AT+LCDBAR=10,10,200,20,0,100,75,0xFFFF,0x06C0,0x0000"), TAG, "register AT failed");
    }
    LCD_LOGI("initialized %dx%d gap=%d,%d", s_state.config.width, s_state.config.height, s_state.config.x_gap, s_state.config.y_gap);
    return ESP_OK;
}

esp_err_t esp_st7789v2_deinit(void)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;

    if (s_state.panel_handle != NULL) {
        (void)esp_lcd_panel_disp_on_off(s_state.panel_handle, false);
        (void)esp_lcd_panel_del(s_state.panel_handle);
    }
    if (s_state.io_handle != NULL) {
        (void)esp_lcd_panel_io_del(s_state.io_handle);
    }
    if (s_state.fill_buffer != NULL) {
        heap_caps_free(s_state.fill_buffer);
    }
    (void)spi_bus_free(s_state.config.host);
    s_state = (st7789_state_t){0};
    return ESP_OK;
}

bool esp_st7789v2_is_initialized(void)
{
    return ready();
}

esp_err_t esp_st7789v2_set_rotation(esp_st7789v2_rotation_t rotation)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    return apply_rotation(rotation);
}

esp_err_t esp_st7789v2_set_invert(bool invert)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_lcd_panel_invert_color(s_state.panel_handle, invert);
    if (err == ESP_OK) {
        s_state.config.invert_color = invert;
        LCD_LOGI("invert=%s", invert ? "on" : "off");
    }
    return err;
}

esp_err_t esp_st7789v2_draw_pixel(int x, int y, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (x < 0 || y < 0 || x >= s_state.config.width || y >= s_state.config.height) return ESP_ERR_INVALID_ARG;
    return esp_st7789v2_fill_rect(x, y, 1, 1, color);
}

esp_err_t esp_st7789v2_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;

    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        esp_err_t draw_err = esp_st7789v2_draw_pixel(x0, y0, color);
        if (draw_err != ESP_OK) return draw_err;
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_hline(int x, int y, int width, uint16_t color)
{
    return esp_st7789v2_fill_rect(x, y, width, 1, color);
}

esp_err_t esp_st7789v2_draw_vline(int x, int y, int height, uint16_t color)
{
    return esp_st7789v2_fill_rect(x, y, 1, height, color);
}

esp_err_t esp_st7789v2_draw_rect(int x, int y, int width, int height, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (width <= 0 || height <= 0 || x < 0 || y < 0) return ESP_ERR_INVALID_ARG;
    if ((x + width) > s_state.config.width || (y + height) > s_state.config.height) return ESP_ERR_INVALID_ARG;

    esp_err_t err = esp_st7789v2_draw_hline(x, y, width, color);
    if (err != ESP_OK) return err;
    err = esp_st7789v2_draw_hline(x, y + height - 1, width, color);
    if (err != ESP_OK) return err;
    err = esp_st7789v2_draw_vline(x, y, height, color);
    if (err != ESP_OK) return err;
    return esp_st7789v2_draw_vline(x + width - 1, y, height, color);
}

esp_err_t esp_st7789v2_draw_round_rect(int x, int y, int width, int height, int radius, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (width <= 0 || height <= 0 || radius < 0) return ESP_ERR_INVALID_ARG;

    int max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) radius = max_radius;
    if (radius == 0) return esp_st7789v2_draw_rect(x, y, width, height, color);

    int inner_w = width - (2 * radius);
    int inner_h = height - (2 * radius);
    if (inner_w > 0) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius, y, inner_w, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius, y + height - 1, inner_w, color), TAG, "round rect failed");
    }
    if (inner_h > 0) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_vline(x, y + radius, inner_h, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_vline(x + width - 1, y + radius, inner_h, color), TAG, "round rect failed");
    }

    int cx1 = x + radius;
    int cy1 = y + radius;
    int cx2 = x + width - radius - 1;
    int cy2 = y + height - radius - 1;
    int dx = radius;
    int dy = 0;
    int err = 1 - dx;

    while (dx >= dy) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx1 - dx, cy1 - dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx1 - dy, cy1 - dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx2 + dx, cy1 - dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx2 + dy, cy1 - dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx1 - dx, cy2 + dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx1 - dy, cy2 + dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx2 + dx, cy2 + dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx2 + dy, cy2 + dx, color), TAG, "round rect failed");

        dy++;
        if (err < 0) {
            err += (2 * dy) + 1;
        } else {
            dx--;
            err += 2 * (dy - dx) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_grid(int x, int y, int width, int height, int cols, int rows, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (cols <= 0 || rows <= 0) return ESP_ERR_INVALID_ARG;

    esp_err_t err = esp_st7789v2_draw_rect(x, y, width, height, color);
    if (err != ESP_OK) return err;

    for (int col = 1; col < cols; col++) {
        int gx = x + ((width * col) / cols);
        err = esp_st7789v2_draw_vline(gx, y, height, color);
        if (err != ESP_OK) return err;
    }
    for (int row = 1; row < rows; row++) {
        int gy = y + ((height * row) / rows);
        err = esp_st7789v2_draw_hline(x, gy, width, color);
        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_circle(int cx, int cy, int radius, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (radius < 0) return ESP_ERR_INVALID_ARG;

    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx + x, cy + y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx + y, cy + x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx - y, cy + x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx - x, cy + y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx - x, cy - y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx - y, cy - x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx + y, cy - x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_pixel(cx + x, cy - y, color), TAG, "circle pixel failed");

        y++;
        if (err < 0) {
            err += (2 * y) + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_fill_circle(int cx, int cy, int radius, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (radius < 0) return ESP_ERR_INVALID_ARG;

    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(cx - x, cy + y, (2 * x) + 1, color), TAG, "fill circle failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(cx - x, cy - y, (2 * x) + 1, color), TAG, "fill circle failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(cx - y, cy + x, (2 * y) + 1, color), TAG, "fill circle failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(cx - y, cy - x, (2 * y) + 1, color), TAG, "fill circle failed");

        y++;
        if (err < 0) {
            err += (2 * y) + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_line(x0, y0, x1, y1, color), TAG, "triangle edge failed");
    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_line(x1, y1, x2, y2, color), TAG, "triangle edge failed");
    return esp_st7789v2_draw_line(x2, y2, x0, y0, color);
}

esp_err_t esp_st7789v2_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;

    if (y0 > y1) { int tx = x0; int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }
    if (y1 > y2) { int tx = x1; int ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    if (y0 > y1) { int tx = x0; int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }

    if (y0 == y2) {
        int min_x = x0;
        int max_x = x0;
        if (x1 < min_x) min_x = x1;
        if (x2 < min_x) min_x = x2;
        if (x1 > max_x) max_x = x1;
        if (x2 > max_x) max_x = x2;
        return esp_st7789v2_draw_hline(min_x, y0, (max_x - min_x) + 1, color);
    }

    for (int y = y0; y <= y2; y++) {
        int xa = x0;
        int xb = x0;

        if (y2 != y0) {
            xa = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0);
        }

        if (y < y1) {
            if (y1 != y0) xb = x0 + ((x1 - x0) * (y - y0)) / (y1 - y0);
            else xb = x1;
        } else {
            if (y2 != y1) xb = x1 + ((x2 - x1) * (y - y1)) / (y2 - y1);
            else xb = x2;
        }

        if (xa > xb) {
            int t = xa;
            xa = xb;
            xb = t;
        }

        ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(xa, y, (xb - xa) + 1, color), TAG, "fill triangle failed");
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_fill_round_rect(int x, int y, int width, int height, int radius, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (width <= 0 || height <= 0 || radius < 0) return ESP_ERR_INVALID_ARG;

    int max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) radius = max_radius;
    if (radius == 0) return esp_st7789v2_fill_rect(x, y, width, height, color);

    int inner_w = width - (2 * radius);
    int inner_h = height - (2 * radius);
    if (inner_w > 0) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_rect(x + radius, y, inner_w, height, color), TAG, "fill round rect failed");
    }
    if (inner_h > 0 && radius > 0) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_rect(x, y + radius, radius, inner_h, color), TAG, "fill round rect failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_rect(x + width - radius, y + radius, radius, inner_h, color), TAG, "fill round rect failed");
    }

    int dx = radius;
    int dy = 0;
    int err = 1 - dx;

    while (dx >= dy) {
        int top_y = y + radius - dx;
        int inner_w1 = width - (2 * (radius - dy));
        int inner_w2 = width - (2 * (radius - dx));

        if (inner_w1 > 0) {
            ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius - dy, top_y, inner_w1, color), TAG, "fill round rect failed");
            ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius - dy, y + height - radius - 1 + dx, inner_w1, color), TAG, "fill round rect failed");
        }
        if (inner_w2 > 0) {
            ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius - dx, y + radius - dy, inner_w2, color), TAG, "fill round rect failed");
            ESP_RETURN_ON_ERROR(esp_st7789v2_draw_hline(x + radius - dx, y + height - radius - 1 + dy, inner_w2, color), TAG, "fill round rect failed");
        }

        dy++;
        if (err < 0) {
            err += (2 * dy) + 1;
        } else {
            dx--;
            err += 2 * (dy - dx) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (width <= 0 || height <= 0 || x < 0 || y < 0) return ESP_ERR_INVALID_ARG;
    if ((x + width) > s_state.config.width || (y + height) > s_state.config.height) return ESP_ERR_INVALID_ARG;

    int tx = 0;
    int ty = 0;
    esp_err_t err = transform_rect(x, y, width, height, &tx, &ty);
    if (err != ESP_OK) return err;

    const size_t fill_lines = s_state.config.fill_lines;
    const size_t line_pixels = (size_t)width * fill_lines;
    if (s_state.fill_buffer == NULL || line_pixels > s_state.fill_buffer_pixels) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t swapped = rgb565_swap(color);
    for (size_t i = 0; i < line_pixels; i++) {
        s_state.fill_buffer[i] = swapped;
    }

    err = ESP_OK;
    int remaining = height;
    int current_y = ty;
    while (remaining > 0) {
        int chunk_lines = (remaining > (int)fill_lines) ? (int)fill_lines : remaining;
        err = esp_lcd_panel_draw_bitmap(s_state.panel_handle, tx, current_y, tx + width, current_y + chunk_lines, s_state.fill_buffer);
        if (err != ESP_OK) break;
        current_y += chunk_lines;
        remaining -= chunk_lines;
    }

    return err;
}

esp_err_t esp_st7789v2_fill_screen(uint16_t color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    return esp_st7789v2_fill_rect(0, 0, s_state.config.width, s_state.config.height, color);
}

esp_err_t esp_st7789v2_clear_area(int x, int y, int width, int height, uint16_t bg)
{
    return esp_st7789v2_fill_rect(x, y, width, height, bg);
}

esp_err_t esp_st7789v2_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (scale <= 0) return ESP_ERR_INVALID_ARG;

    const uint8_t *glyph = font5x7_for_char(c);
    const int char_w = 5 * scale;
    const int char_h = 7 * scale;
    if (x < 0 || y < 0 || (x + char_w) > s_state.config.width || (y + char_h) > s_state.config.height) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            bool on = (glyph[row] & (1U << (4 - col))) != 0U;
            esp_err_t err = esp_st7789v2_fill_rect(x + (col * scale), y + (row * scale), scale, scale, on ? fg : bg);
            if (err != ESP_OK) return err;
        }
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (text == NULL || scale <= 0) return ESP_ERR_INVALID_ARG;

    const int advance = 6 * scale;
    const size_t len = strlen(text);
    if (len == 0U) return ESP_OK;
    if (x < 0 || y < 0) return ESP_ERR_INVALID_ARG;
    if ((x + ((int)len * advance) - scale) > s_state.config.width || (y + (7 * scale)) > s_state.config.height) {
        return ESP_ERR_INVALID_ARG;
    }

    int cursor_x = x;
    for (size_t i = 0; i < len; i++) {
        esp_err_t err = esp_st7789v2_draw_char(cursor_x, y, text[i], fg, bg, scale);
        if (err != ESP_OK) return err;
        if (bg != fg) {
            err = esp_st7789v2_fill_rect(cursor_x + (5 * scale), y, scale, 7 * scale, bg);
            if (err != ESP_OK) return err;
        }
        cursor_x += advance;
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_7seg_char(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (height <= 0 || thickness <= 0) return ESP_ERR_INVALID_ARG;

    const int width = ((height * 3) / 5 > thickness * 3) ? (height * 3) / 5 : thickness * 3;
    const int radius = thickness / 2;
    const int pad = thickness / 3;
    const int hseg_x = x + (thickness / 2);
    const int hseg_w = width - thickness;
    const int vseg_h = (height - (3 * thickness)) / 2;
    const int left_x = x;
    const int right_x = x + width - thickness;
    const int top_y = y;
    const int mid_y = y + thickness + vseg_h;
    const int upper_y = y + thickness;
    const int lower_y = y + (2 * thickness) + vseg_h;
    const int bottom_y = y + height - thickness;

    if (hseg_w <= 0 || vseg_h <= 0) return ESP_ERR_INVALID_ARG;
    if (width <= (2 * thickness)) return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(esp_st7789v2_clear_area(x, y, width, height, bg), TAG, "7seg clear failed");

    if (c == ' ') return ESP_OK;
    if (c == 'o' || c == 'O' || c == '*') {
        int seg_t = (thickness * 2) / 3;
        if (seg_t < 3) seg_t = 3;

        int mini_w = (width * 2) / 3;
        int mini_h = height / 2;
        if (mini_w < (seg_t * 4)) mini_w = seg_t * 4;
        if (mini_h < (seg_t * 4)) mini_h = seg_t * 4;

        int mini_x = x + width - mini_w - 1;
        int mini_y = y;
        int mini_v = (mini_h - (2 * seg_t)) / 2;
        int mini_inner_w = mini_w - seg_t;
        int mini_r = seg_t / 2;

        if (mini_x < x) mini_x = x;
        if (mini_v <= 0 || mini_inner_w <= 0) return ESP_ERR_INVALID_ARG;

        /* degree symbol as mini 7-seg with A, B, G, F on */
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(mini_x + (seg_t / 2), mini_y, mini_inner_w, seg_t, mini_r, fg), TAG, "7seg degree failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(mini_x, mini_y + seg_t, seg_t, mini_v, mini_r, fg), TAG, "7seg degree failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(mini_x + mini_w - seg_t, mini_y + seg_t, seg_t, mini_v, mini_r, fg), TAG, "7seg degree failed");
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(mini_x + (seg_t / 2), mini_y + seg_t + mini_v, mini_inner_w, seg_t, mini_r, fg), TAG, "7seg degree failed");
        return ESP_OK;
    }
    if (c == '.') {
        return esp_st7789v2_fill_circle(x + width - radius - 1, y + height - radius - 1, radius > 0 ? radius : 1, fg);
    }
    if (c == ':') {
        int dot_r = radius > 0 ? radius : 1;
        int dot_x = x + (width / 2);
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_circle(dot_x, y + (height / 3), dot_r, fg), TAG, "7seg colon failed");
        return esp_st7789v2_fill_circle(dot_x, y + ((height * 2) / 3), dot_r, fg);
    }
    if (c == ',') {
        int dot_r = radius > 0 ? radius : 1;
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_circle(x + width - dot_r - 1, y + height - dot_r - 1, dot_r, fg), TAG, "7seg comma failed");
        return esp_st7789v2_draw_line(x + width - dot_r - 1, y + height - 1, x + width - thickness - pad, y + height + thickness - 1, fg);
    }

    uint8_t mask = 0;
    switch (c) {
        case '0': mask = 0x3F; break;
        case '1': mask = 0x06; break;
        case '2': mask = 0x5B; break;
        case '3': mask = 0x4F; break;
        case '4': mask = 0x66; break;
        case '5': mask = 0x6D; break;
        case '6': mask = 0x7D; break;
        case '7': mask = 0x07; break;
        case '8': mask = 0x7F; break;
        case '9': mask = 0x6F; break;
        case 'A':
        case 'a': mask = 0x77; break;
        case 'B':
        case 'b': mask = 0x7C; break;
        case 'C':
        case 'c': mask = 0x39; break;
        case 'D':
        case 'd': mask = 0x5E; break;
        case 'E':
        case 'e': mask = 0x79; break;
        case 'F':
        case 'f': mask = 0x71; break;
        case 'G':
        case 'g': mask = 0x6F; break;
        case 'H':
        case 'h': mask = 0x76; break;
        case 'I':
        case 'i': mask = 0x06; break;
        case 'J':
        case 'j': mask = 0x1E; break;
        case 'L':
        case 'l': mask = 0x38; break;
        case 'N':
        case 'n': mask = 0x54; break;
        case 'O':
            break;
        case 'P':
        case 'p': mask = 0x73; break;
        case 'R':
        case 'r': mask = 0x50; break;
        case 'S':
        case 's': mask = 0x6D; break;
        case 'T':
        case 't': mask = 0x78; break;
        case 'U':
        case 'u': mask = 0x3E; break;
        case 'Y':
        case 'y': mask = 0x6E; break;
        default: return ESP_OK;
    }

    if (mask & 0x01) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(hseg_x, top_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");
    if (mask & 0x02) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(right_x, upper_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x04) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(right_x, lower_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x08) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(hseg_x, bottom_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");
    if (mask & 0x10) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(left_x, lower_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x20) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(left_x, upper_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x40) ESP_RETURN_ON_ERROR(esp_st7789v2_fill_round_rect(hseg_x, mid_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");

    return ESP_OK;
}

int esp_st7789v2_7seg_text_width(const char *text, int height, int thickness)
{
    if (text == NULL || height <= 0 || thickness <= 0) return 0;

    const int width = ((height * 3) / 5 > thickness * 3) ? (height * 3) / 5 : thickness * 3;
    const int spacing = thickness;
    int glyphs = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        if ((unsigned char)text[i] == 0xC2 && (unsigned char)text[i + 1] == 0xB0) {
            i++;
        }
        glyphs++;
    }
    if (glyphs == 0) return 0;
    return (glyphs * (width + spacing)) - spacing;
}

esp_err_t esp_st7789v2_draw_7seg_text(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (text == NULL || height <= 0 || thickness <= 0) return ESP_ERR_INVALID_ARG;

    const int width = ((height * 3) / 5 > thickness * 3) ? (height * 3) / 5 : thickness * 3;
    const int spacing = thickness;
    int cursor_x = x;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if ((unsigned char)text[i] == 0xC2 && (unsigned char)text[i + 1] == 0xB0) {
            c = 'O';
            i++;
        }
        esp_err_t err = esp_st7789v2_draw_7seg_char(cursor_x, y, c, height, thickness, fg, bg);
        if (err != ESP_OK) return err;
        cursor_x += width + spacing;
    }

    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_7seg_box(int x, int y, int width, int height, int thickness, const char *text, uint16_t fg, uint16_t bg, esp_st7789v2_align_t align)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (text == NULL || width <= 0 || height <= 0 || thickness <= 0) return ESP_ERR_INVALID_ARG;

    int text_width = esp_st7789v2_7seg_text_width(text, height, thickness);
    if (text_width <= 0 || text_width > width) return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(esp_st7789v2_clear_area(x, y, width, height, bg), TAG, "7seg box clear failed");

    int draw_x = x;
    switch (align) {
        case ESP_ST7789V2_ALIGN_LEFT:
            draw_x = x;
            break;
        case ESP_ST7789V2_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case ESP_ST7789V2_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return esp_st7789v2_draw_7seg_text(draw_x, y, text, height, thickness, fg, bg);
}

esp_err_t esp_st7789v2_update_7seg_box_if_changed(esp_st7789v2_7seg_box_t *box, const char *text)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (box == NULL || text == NULL) return ESP_ERR_INVALID_ARG;
    if (!box->initialized) return ESP_ERR_INVALID_STATE;
    if (strncmp(box->last_text, text, sizeof(box->last_text)) == 0) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_7seg_box(box->x, box->y, box->width, box->height, box->thickness, text, box->fg, box->bg, box->align), TAG, "7seg box update failed");
    strncpy(box->last_text, text, sizeof(box->last_text) - 1U);
    box->last_text[sizeof(box->last_text) - 1U] = '\0';
    return ESP_OK;
}

int esp_st7789v2_text_width(const char *text, int scale)
{
    if (text == NULL || scale <= 0) return 0;

    size_t len = strlen(text);
    if (len == 0U) return 0;
    return (int)(((len * 6U) - 1U) * (size_t)scale);
}

esp_err_t esp_st7789v2_draw_text_box(int x, int y, int width, int height, const char *text, uint16_t fg, uint16_t bg, int scale, esp_st7789v2_align_t align)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (text == NULL || width <= 0 || height <= 0 || scale <= 0) return ESP_ERR_INVALID_ARG;

    int text_width = esp_st7789v2_text_width(text, scale);
    int text_height = 7 * scale;
    if (text_width <= 0 || text_height <= 0) return ESP_ERR_INVALID_ARG;
    if (text_width > width || text_height > height) return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(esp_st7789v2_clear_area(x, y, width, height, bg), TAG, "clear area failed");

    int draw_x = x;
    switch (align) {
        case ESP_ST7789V2_ALIGN_LEFT:
            draw_x = x;
            break;
        case ESP_ST7789V2_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case ESP_ST7789V2_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    int draw_y = y + ((height - text_height) / 2);
    return esp_st7789v2_draw_text(draw_x, draw_y, text, fg, bg, scale);
}

esp_err_t esp_st7789v2_update_text_box_if_changed(esp_st7789v2_text_box_t *box, const char *text)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (box == NULL || text == NULL) return ESP_ERR_INVALID_ARG;
    if (!box->initialized) return ESP_ERR_INVALID_STATE;

    if (strncmp(box->last_text, text, sizeof(box->last_text)) == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_text_box(box->x, box->y, box->width, box->height, text, box->fg, box->bg, box->scale, box->align), TAG, "text box update failed");
    strncpy(box->last_text, text, sizeof(box->last_text) - 1U);
    box->last_text[sizeof(box->last_text) - 1U] = '\0';
    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_progress_bar(int x, int y, int width, int height, int min, int max, int value, uint16_t border_color, uint16_t fill_color, uint16_t bg_color)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (width <= 2 || height <= 2) return ESP_ERR_INVALID_ARG;
    if (max <= min) return ESP_ERR_INVALID_ARG;

    if (value < min) value = min;
    if (value > max) value = max;

    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = width - 2;
    const int inner_h = height - 2;
    const int range = max - min;
    const int fill_w = (inner_w * (value - min)) / range;

    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_rect(x, y, width, height, border_color), TAG, "progress border failed");
    ESP_RETURN_ON_ERROR(esp_st7789v2_fill_rect(inner_x, inner_y, inner_w, inner_h, bg_color), TAG, "progress bg failed");
    if (fill_w > 0) {
        ESP_RETURN_ON_ERROR(esp_st7789v2_fill_rect(inner_x, inner_y, fill_w, inner_h, fill_color), TAG, "progress fill failed");
    }
    return ESP_OK;
}

esp_err_t esp_st7789v2_update_progress_bar_if_changed(esp_st7789v2_progress_bar_t *bar, int value)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (bar == NULL) return ESP_ERR_INVALID_ARG;
    if (!bar->initialized) return ESP_ERR_INVALID_STATE;
    if (bar->value == value) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_st7789v2_draw_progress_bar(bar->x, bar->y, bar->width, bar->height, bar->min, bar->max, value, bar->border_color, bar->fill_color, bar->bg_color), TAG, "progress update failed");
    bar->value = value;
    return ESP_OK;
}

esp_err_t esp_st7789v2_draw_text_aligned(int y, const char *text, uint16_t fg, uint16_t bg, int scale, esp_st7789v2_align_t align)
{
    if (!ready()) return ESP_ERR_INVALID_STATE;
    if (text == NULL || scale <= 0) return ESP_ERR_INVALID_ARG;

    int text_width = esp_st7789v2_text_width(text, scale);
    int x = 0;
    switch (align) {
        case ESP_ST7789V2_ALIGN_LEFT:
            x = 0;
            break;
        case ESP_ST7789V2_ALIGN_CENTER:
            x = (s_state.config.width - text_width) / 2;
            break;
        case ESP_ST7789V2_ALIGN_RIGHT:
            x = s_state.config.width - text_width;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    if (x < 0) return ESP_ERR_INVALID_ARG;
    return esp_st7789v2_draw_text(x, y, text, fg, bg, scale);
}

int esp_st7789v2_width(void)
{
    return ready() ? s_state.config.width : 0;
}

int esp_st7789v2_height(void)
{
    return ready() ? s_state.config.height : 0;
}

static esp_err_t transform_rect(int x, int y, int width, int height, int *out_x, int *out_y)
{
    if (out_x == NULL || out_y == NULL) return ESP_ERR_INVALID_ARG;

    int tx = x;
    int ty = y;

    if (s_state.config.mirror_y) {
        tx = s_state.config.width - x - width;
    }
    if (!s_state.config.mirror_x) {
        ty = s_state.config.height - y - height;
    }

    *out_x = tx;
    *out_y = ty;
    return ESP_OK;
}

static void handle_at_lcdclr(const char *param)
{
    uint16_t color = BLACK;
    if (param != NULL && *param != '\0') {
        char work[32];
        strncpy(work, param, sizeof(work) - 1U);
        work[sizeof(work) - 1U] = '\0';
        if (parse_u16_color(trim_ws(work), &color) != ESP_OK) {
            AT(R "ERROR: use AT+LCDCLR=<cor>");
            return;
        }
    }

    esp_err_t err = esp_st7789v2_fill_screen(color);
    if (err != ESP_OK) {
        AT(R "ERROR: clear failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdpx(const char *param)
{
    int x = 0;
    int y = 0;
    uint16_t color = 0;
    if (parse_pixel_params(param, &x, &y, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDPX=x,y,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_pixel(x, y, color);
    if (err != ESP_OK) {
        AT(R "ERROR: pixel failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdline(const char *param)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint16_t color = 0;
    if (parse_line_params(param, &x0, &y0, &x1, &y1, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDLINE=x0,y0,x1,y1,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_line(x0, y0, x1, y1, color);
    if (err != ESP_OK) {
        AT(R "ERROR: line failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdhl(const char *param)
{
    int x = 0, y = 0, width = 0, unused = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &x, &y, &width, &unused, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDHL=x,y,width,0,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_hline(x, y, width, color);
    if (err != ESP_OK) {
        AT(R "ERROR: hline failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdvl(const char *param)
{
    int x = 0, y = 0, unused = 0, height = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &x, &y, &unused, &height, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDVL=x,y,0,height,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_vline(x, y, height, color);
    if (err != ESP_OK) {
        AT(R "ERROR: vline failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdrect(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &x, &y, &width, &height, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDRECT=x,y,w,h,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_rect(x, y, width, height, color);
    if (err != ESP_OK) {
        AT(R "ERROR: rect failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdrrect(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0, radius = 0;
    uint16_t color = 0;
    if (parse_round_rect_params(param, &x, &y, &width, &height, &radius, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDRRECT=x,y,w,h,r,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_round_rect(x, y, width, height, radius, color);
    if (err != ESP_OK) {
        AT(R "ERROR: round rect failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdfill(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &x, &y, &width, &height, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDFILL=x,y,w,h,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_fill_rect(x, y, width, height, color);
    if (err != ESP_OK) {
        AT(R "ERROR: fill failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdfrrect(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0, radius = 0;
    uint16_t color = 0;
    if (parse_round_rect_params(param, &x, &y, &width, &height, &radius, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDFRRECT=x,y,w,h,r,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_fill_round_rect(x, y, width, height, radius, color);
    if (err != ESP_OK) {
        AT(R "ERROR: fill round rect failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdgrid(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0, cols = 0, rows = 0;
    uint16_t color = 0;
    if (parse_grid_params(param, &x, &y, &width, &height, &cols, &rows, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDGRID=x,y,w,h,cols,rows,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_grid(x, y, width, height, cols, rows, color);
    if (err != ESP_OK) {
        AT(R "ERROR: grid failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdcirc(const char *param)
{
    int cx = 0, cy = 0, radius = 0, unused = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &cx, &cy, &radius, &unused, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDCIRC=cx,cy,r,0,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_circle(cx, cy, radius, color);
    if (err != ESP_OK) {
        AT(R "ERROR: circle failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdfcirc(const char *param)
{
    int cx = 0, cy = 0, radius = 0, unused = 0;
    uint16_t color = 0;
    if (parse_rect_params(param, &cx, &cy, &radius, &unused, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDFCIRC=cx,cy,r,0,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_fill_circle(cx, cy, radius, color);
    if (err != ESP_OK) {
        AT(R "ERROR: fill circle failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdtri(const char *param)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint16_t color = 0;
    if (parse_triangle_params(param, &x0, &y0, &x1, &y1, &x2, &y2, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDTRI=x0,y0,x1,y1,x2,y2,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_triangle(x0, y0, x1, y1, x2, y2, color);
    if (err != ESP_OK) {
        AT(R "ERROR: triangle failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdftri(const char *param)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint16_t color = 0;
    if (parse_triangle_params(param, &x0, &y0, &x1, &y1, &x2, &y2, &color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDFTRI=x0,y0,x1,y1,x2,y2,cor");
        return;
    }

    esp_err_t err = esp_st7789v2_fill_triangle(x0, y0, x1, y1, x2, y2, color);
    if (err != ESP_OK) {
        AT(R "ERROR: fill triangle failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdtxt(const char *param)
{
    int x = 0, y = 0, scale = 0;
    uint16_t fg = 0, bg = 0;
    const char *text = NULL;
    if (parse_text_params(param, &x, &y, &scale, &fg, &bg, &text) != ESP_OK) {
        AT(R "ERROR: use AT+LCDTXT=x,y,scale,fg,bg,text");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_text(x, y, text, fg, bg, scale);
    if (err != ESP_OK) {
        AT(R "ERROR: text failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdbox(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0, scale = 0;
    uint16_t fg = 0, bg = 0;
    esp_st7789v2_align_t align = ESP_ST7789V2_ALIGN_LEFT;
    const char *text = NULL;
    if (parse_text_box_params(param, &x, &y, &width, &height, &scale, &fg, &bg, &align, &text) != ESP_OK) {
        AT(R "ERROR: use AT+LCDBOX=x,y,w,h,scale,fg,bg,LEFT|CENTER|RIGHT,text");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_text_box(x, y, width, height, text, fg, bg, scale, align);
    if (err != ESP_OK) {
        AT(R "ERROR: text box failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcd7seg(const char *param)
{
    int x = 0, y = 0, height = 0, thickness = 0;
    uint16_t fg = 0, bg = 0;
    const char *text = NULL;
    if (parse_7seg_params(param, &x, &y, &height, &thickness, &fg, &bg, &text) != ESP_OK) {
        AT(R "ERROR: use AT+LCD7SEG=x,y,height,thickness,fg,bg,text");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_7seg_text(x, y, text, height, thickness, fg, bg);
    if (err != ESP_OK) {
        AT(R "ERROR: 7seg failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcd7box(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0, thickness = 0;
    uint16_t fg = 0, bg = 0;
    esp_st7789v2_align_t align = ESP_ST7789V2_ALIGN_LEFT;
    const char *text = NULL;
    if (parse_7seg_box_params(param, &x, &y, &width, &height, &thickness, &fg, &bg, &align, &text) != ESP_OK) {
        AT(R "ERROR: use AT+LCD7BOX=x,y,w,h,t,fg,bg,LEFT|CENTER|RIGHT,text");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_7seg_box(x, y, width, height, thickness, text, fg, bg, align);
    if (err != ESP_OK) {
        AT(R "ERROR: 7seg box failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static void handle_at_lcdbar(const char *param)
{
    int x = 0, y = 0, width = 0, height = 0;
    int min = 0, max = 0, value = 0;
    uint16_t border_color = 0, fill_color = 0, bg_color = 0;
    if (parse_progress_params(param, &x, &y, &width, &height, &min, &max, &value, &border_color, &fill_color, &bg_color) != ESP_OK) {
        AT(R "ERROR: use AT+LCDBAR=x,y,w,h,min,max,value,border,fill,bg");
        return;
    }

    esp_err_t err = esp_st7789v2_draw_progress_bar(x, y, width, height, min, max, value, border_color, fill_color, bg_color);
    if (err != ESP_OK) {
        AT(R "ERROR: progress failed (%s)", esp_err_to_name(err));
        return;
    }
    AT(G "OK");
}

static esp_err_t apply_rotation(esp_st7789v2_rotation_t rotation)
{
    esp_st7789v2_config_t rotated = s_state.base_config;
    switch (rotation) {
        case ESP_ST7789V2_ROTATION_0:
            rotated.width = s_state.base_config.width;
            rotated.height = s_state.base_config.height;
            rotated.x_gap = s_state.base_config.x_gap;
            rotated.y_gap = s_state.base_config.y_gap;
            rotated.swap_xy = true;
            rotated.mirror_x = false;
            rotated.mirror_y = true;
            break;
        case ESP_ST7789V2_ROTATION_90:
            rotated.width = s_state.base_config.height;
            rotated.height = s_state.base_config.width;
            rotated.x_gap = s_state.base_config.y_gap;
            rotated.y_gap = s_state.base_config.x_gap;
            rotated.swap_xy = false;
            rotated.mirror_x = false;
            rotated.mirror_y = false;
            break;
        case ESP_ST7789V2_ROTATION_180:
            rotated.width = s_state.base_config.width;
            rotated.height = s_state.base_config.height;
            rotated.x_gap = s_state.base_config.x_gap;
            rotated.y_gap = s_state.base_config.y_gap;
            rotated.swap_xy = true;
            rotated.mirror_x = true;
            rotated.mirror_y = false;
            break;
        case ESP_ST7789V2_ROTATION_270:
            rotated.width = s_state.base_config.height;
            rotated.height = s_state.base_config.width;
            rotated.x_gap = s_state.base_config.y_gap;
            rotated.y_gap = s_state.base_config.x_gap;
            rotated.swap_xy = false;
            rotated.mirror_x = true;
            rotated.mirror_y = true;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_lcd_panel_swap_xy(s_state.panel_handle, rotated.swap_xy);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_mirror(s_state.panel_handle, rotated.mirror_x, rotated.mirror_y);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_set_gap(s_state.panel_handle, rotated.x_gap, rotated.y_gap);
    if (err != ESP_OK) return err;

    s_state.config.width = rotated.width;
    s_state.config.height = rotated.height;
    s_state.config.x_gap = rotated.x_gap;
    s_state.config.y_gap = rotated.y_gap;
    s_state.config.swap_xy = rotated.swap_xy;
    s_state.config.mirror_x = rotated.mirror_x;
    s_state.config.mirror_y = rotated.mirror_y;
    s_state.rotation = rotation;

    LCD_LOGI("rotation=%d size=%dx%d gap=%d,%d", (int)rotation, rotated.width, rotated.height, rotated.x_gap, rotated.y_gap);
    return ESP_OK;
}

static esp_err_t esp_st7789v2_new_panel_st7789(const esp_lcd_panel_io_handle_t io,
                                               const esp_lcd_panel_dev_config_t *panel_dev_config,
                                               esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    st7789_panel_t *st7789 = NULL;
    gpio_config_t io_conf = {0};

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    st7789 = calloc(1, sizeof(*st7789));
    ESP_GOTO_ON_FALSE(st7789, ESP_ERR_NO_MEM, err, TAG, "no mem for st7789 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
        case LCD_RGB_ELEMENT_ORDER_RGB:
            st7789->madctl_val = 0;
            break;
        case LCD_RGB_ELEMENT_ORDER_BGR:
            st7789->madctl_val = LCD_CMD_BGR_BIT;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported rgb element order");
            break;
    }

    switch (panel_dev_config->bits_per_pixel) {
        case 16:
            st7789->colmod_val = 0x55;
            st7789->fb_bits_per_pixel = 16;
            break;
        case 18:
            st7789->colmod_val = 0x66;
            st7789->fb_bits_per_pixel = 24;
            break;
        default:
            ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
            break;
    }

    st7789->io = io;
    st7789->reset_gpio_num = panel_dev_config->reset_gpio_num;
    st7789->reset_level = panel_dev_config->flags.reset_active_high;
    if (panel_dev_config->vendor_config != NULL) {
        st7789->init_cmds = ((st7789_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds;
        st7789->init_cmds_size = ((st7789_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds_size;
    }

    st7789->base.del = panel_st7789_del;
    st7789->base.reset = panel_st7789_reset;
    st7789->base.init = panel_st7789_init;
    st7789->base.draw_bitmap = panel_st7789_draw_bitmap;
    st7789->base.invert_color = panel_st7789_invert_color;
    st7789->base.set_gap = panel_st7789_set_gap;
    st7789->base.mirror = panel_st7789_mirror;
    st7789->base.swap_xy = panel_st7789_swap_xy;
    st7789->base.disp_on_off = panel_st7789_disp_on_off;
    *ret_panel = &st7789->base;

    return ESP_OK;

err:
    if (st7789 != NULL) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(st7789);
    }
    return ret;
}

static esp_err_t panel_st7789_del(esp_lcd_panel_t *panel)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    if (st7789->reset_gpio_num >= 0) {
        gpio_reset_pin(st7789->reset_gpio_num);
    }
    free(st7789);
    return ESP_OK;
}

static esp_err_t panel_st7789_reset(esp_lcd_panel_t *panel)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7789->io;

    if (st7789->reset_gpio_num >= 0) {
        gpio_set_level(st7789->reset_gpio_num, st7789->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(st7789->reset_gpio_num, !st7789->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t panel_st7789_init(esp_lcd_panel_t *panel)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7789->io;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){st7789->madctl_val}, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]){st7789->colmod_val}, 1), TAG, "send command failed");

    const st7789_lcd_init_cmd_t *init_cmds = st7789->init_cmds ? st7789->init_cmds : vendor_specific_init_default;
    uint16_t init_cmds_size = st7789->init_cmds ? st7789->init_cmds_size : (sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]));

    for (uint16_t i = 0; i < init_cmds_size; i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    return ESP_OK;
}

static esp_err_t panel_st7789_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    esp_lcd_panel_io_handle_t io = st7789->io;

    assert((x_start < x_end) && (y_start < y_end));

    x_start += st7789->x_gap;
    x_end += st7789->x_gap;
    y_start += st7789->y_gap;
    y_end += st7789->y_gap;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET,
                                                  (uint8_t[]){
                                                      (x_start >> 8) & 0xFF,
                                                      x_start & 0xFF,
                                                      ((x_end - 1) >> 8) & 0xFF,
                                                      (x_end - 1) & 0xFF,
                                                  },
                                                  4),
                        TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET,
                                                  (uint8_t[]){
                                                      (y_start >> 8) & 0xFF,
                                                      y_start & 0xFF,
                                                      ((y_end - 1) >> 8) & 0xFF,
                                                      (y_end - 1) & 0xFF,
                                                  },
                                                  4),
                        TAG, "send command failed");

    size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * st7789->fb_bits_per_pixel / 8U;
    return esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);
}

static esp_err_t panel_st7789_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    return esp_lcd_panel_io_tx_param(st7789->io, invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}

static esp_err_t panel_st7789_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    if (mirror_x) st7789->madctl_val |= LCD_CMD_MX_BIT;
    else st7789->madctl_val &= (uint8_t)~LCD_CMD_MX_BIT;
    if (mirror_y) st7789->madctl_val |= LCD_CMD_MY_BIT;
    else st7789->madctl_val &= (uint8_t)~LCD_CMD_MY_BIT;
    return esp_lcd_panel_io_tx_param(st7789->io, LCD_CMD_MADCTL, (uint8_t[]){st7789->madctl_val}, 1);
}

static esp_err_t panel_st7789_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    if (swap_axes) st7789->madctl_val |= LCD_CMD_MV_BIT;
    else st7789->madctl_val &= (uint8_t)~LCD_CMD_MV_BIT;
    return esp_lcd_panel_io_tx_param(st7789->io, LCD_CMD_MADCTL, (uint8_t[]){st7789->madctl_val}, 1);
}

static esp_err_t panel_st7789_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    st7789->x_gap = x_gap;
    st7789->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_st7789_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st7789_panel_t *st7789 = __containerof(panel, st7789_panel_t, base);
    return esp_lcd_panel_io_tx_param(st7789->io, on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}
