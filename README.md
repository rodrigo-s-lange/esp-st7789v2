# esp_st7789v2

Calibrated local ST7789 driver for the project display.

## Goals

- fast panel bring-up
- direct drawing API
- sector-only updates to avoid flicker
- optional AT commands for bench validation

## Validated base

- visible origin and rotation calibrated on hardware
- stable clear, pixel and rectangle operations
- calibrated RGB565 palette for the target panel
- partial redraw helpers for UI sectors

## API groups

Initialization:
- `esp_st7789v2_init(log_enabled, at_enabled)`
- `esp_st7789v2_deinit()`
- `esp_st7789v2_is_initialized()`

Panel:
- `esp_st7789v2_set_rotation(...)`
- `esp_st7789v2_set_invert(...)`
- `esp_st7789v2_width()`
- `esp_st7789v2_height()`

Geometry:
- `draw_pixel`, `draw_line`, `draw_hline`, `draw_vline`
- `draw_rect`, `draw_round_rect`, `draw_grid`
- `draw_circle`, `fill_circle`
- `draw_triangle`, `fill_triangle`
- `fill_rect`, `fill_round_rect`, `fill_screen`, `clear_area`

Text:
- `draw_char`, `draw_text`, `draw_text_aligned`
- `draw_text_box`, `update_text_box_if_changed`
- `text_width`

7-segment:
- `draw_7seg_char`, `draw_7seg_text`
- `draw_7seg_box`, `update_7seg_box_if_changed`
- `7seg_text_width`

Widgets:
- `draw_progress_bar`
- `update_progress_bar_if_changed`

## Partial updates

To avoid display flicker:
- do not clear the full screen for every value change
- clear only the affected area
- redraw only the changed sector

Helpers meant for this flow:
- `esp_st7789v2_clear_area(...)`
- `esp_st7789v2_draw_text_box(...)`
- `esp_st7789v2_update_text_box_if_changed(...)`
- `esp_st7789v2_draw_7seg_box(...)`
- `esp_st7789v2_update_7seg_box_if_changed(...)`
- `esp_st7789v2_draw_progress_bar(...)`
- `esp_st7789v2_update_progress_bar_if_changed(...)`

## AT commands

When `esp_st7789v2_init(log_enabled, true)` is used, the component registers validation commands documented in `COMMANDS.md`.

## Examples

See `EXAMPLES.md`.
