# esp_st7789v2

Minimal ST7789 driver over native ESP-IDF `esp_lcd`.

Initial scope:

- SPI panel init
- reset / display on
- backlight on/off
- rotation
- invert color
- draw pixel
- fill rect / fill screen
- draw RGB565 bitmap
- 7-segment numeric font

This first version is aimed at `170x320` panels and keeps the API focused.

## API

- `esp_st7789v2_init()`
- `esp_st7789v2_deinit()`
- `esp_st7789v2_set_rotation()`
- `esp_st7789v2_set_invert()`
- `esp_st7789v2_set_backlight()`
- `esp_st7789v2_display_on()`
- `esp_st7789v2_draw_pixel()`
- `esp_st7789v2_fill_rect()`
- `esp_st7789v2_fill_screen()`
- `esp_st7789v2_draw_bitmap()`
- `esp_st7789v2_draw_7seg_char()`
- `esp_st7789v2_draw_7seg_text()`

## Notes

- Uses RGB565.
- Uses `esp_lcd` instead of raw SPI command code.
- Rotation/gap values may need tuning depending on the exact panel module.
- Includes a lightweight 7-segment renderer for:
  - `0..9`
  - `.`
  - `,`
  - `:`
  - `/`
  - `+`
  - `-`
  - `°`

