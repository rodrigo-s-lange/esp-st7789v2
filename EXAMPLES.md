# esp_st7789v2 examples

## Basic init

```c
#include "esp_st7789v2.h"

esp_st7789v2_t *lcd = NULL;

esp_st7789v2_config_t cfg = {
    .host = SPI2_HOST,
    .pin_sclk = GPIO_NUM_18,
    .pin_mosi = GPIO_NUM_23,
    .pin_cs = GPIO_NUM_5,
    .pin_dc = GPIO_NUM_16,
    .pin_rst = GPIO_NUM_4,
    .pin_backlight = GPIO_NUM_17,
    .pixel_clock_hz = ESP_ST7789V2_DEFAULT_PCLK_HZ,
    .width = 170,
    .height = 320,
    .x_gap = 35,
    .y_gap = 0,
    .rgb_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .backlight_active_high = true,
};

ESP_ERROR_CHECK(esp_st7789v2_init(&cfg, &lcd));
ESP_ERROR_CHECK(esp_st7789v2_fill_screen(lcd, 0x0000));
```

## Draw a red rectangle

```c
ESP_ERROR_CHECK(esp_st7789v2_fill_rect(lcd, 10, 10, 100, 40, 0xF800));
```

## Draw 7-segment text

```c
ESP_ERROR_CHECK(esp_st7789v2_draw_7seg_text(lcd, 10, 20, "12:45", 48, 6, 0xFFE0, 0x2104));
ESP_ERROR_CHECK(esp_st7789v2_draw_7seg_text(lcd, 10, 90, "-12.5°", 36, 5, 0xF800, 0x0000));
```

