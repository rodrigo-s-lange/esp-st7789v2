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
static esp_err_t parse_i32(const char *text, int *out_value);
static esp_err_t parse_u16_color(const char *text, uint16_t *out_color);
static esp_err_t parse_pixel_params(const char *param, int *x, int *y, uint16_t *color);
static esp_err_t parse_rect_params(const char *param, int *a, int *b, int *c, int *d, uint16_t *color);
static esp_err_t parse_grid_params(const char *param, int *x, int *y, int *width, int *height, int *cols, int *rows, uint16_t *color);
static esp_err_t parse_text_params(const char *param, int *x, int *y, int *scale, uint16_t *fg, uint16_t *bg, const char **text);
static void handle_at_lcdclr(const char *param);
static void handle_at_lcdpx(const char *param);
static void handle_at_lcdhl(const char *param);
static void handle_at_lcdvl(const char *param);
static void handle_at_lcdrect(const char *param);
static void handle_at_lcdfill(const char *param);
static void handle_at_lcdgrid(const char *param);
static void handle_at_lcdtxt(const char *param);

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
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDHL", handle_at_lcdhl, "AT+LCDHL=10,20,100,0,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDVL", handle_at_lcdvl, "AT+LCDVL=20,10,0,80,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDRECT", handle_at_lcdrect, "AT+LCDRECT=10,10,80,40,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDFILL", handle_at_lcdfill, "AT+LCDFILL=10,10,80,40,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDGRID", handle_at_lcdgrid, "AT+LCDGRID=10,10,300,150,10,5,0xFFFF"), TAG, "register AT failed");
        ESP_RETURN_ON_ERROR(esp_at_register_cmd_example("AT+LCDTXT", handle_at_lcdtxt, "AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO"), TAG, "register AT failed");
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

int esp_st7789v2_text_width(const char *text, int scale)
{
    if (text == NULL || scale <= 0) return 0;

    size_t len = strlen(text);
    if (len == 0U) return 0;
    return (int)(((len * 6U) - 1U) * (size_t)scale);
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
