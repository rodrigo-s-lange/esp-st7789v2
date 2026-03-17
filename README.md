# esp_st7789v2

Driver local para ST7789 170x320 baseado em `esp_lcd`, calibrado para o painel validado no projeto.

Estado atual:
- rotacao calibrada com origem visivel em `0,0`
- paleta RGB565 calibrada no hardware
- primitivas basicas:
  - pixel
  - line
  - hline
  - vline
  - rect
  - round rect
  - fill
  - grid
  - circle
  - fill circle
  - triangle
- texto bitmap `5x7` com `scale`
- alinhamento de texto:
  - `LEFT`
  - `CENTER`
  - `RIGHT`
- comandos AT de validacao opcionais registrados pela propria lib

API principal:
- `esp_st7789v2_init(log_enabled, at_enabled)`
- `esp_st7789v2_set_rotation(...)`
- `esp_st7789v2_set_invert(...)`
- `esp_st7789v2_draw_pixel(...)`
- `esp_st7789v2_draw_line(...)`
- `esp_st7789v2_draw_hline(...)`
- `esp_st7789v2_draw_vline(...)`
- `esp_st7789v2_draw_rect(...)`
- `esp_st7789v2_draw_round_rect(...)`
- `esp_st7789v2_draw_grid(...)`
- `esp_st7789v2_draw_circle(...)`
- `esp_st7789v2_fill_circle(...)`
- `esp_st7789v2_draw_triangle(...)`
- `esp_st7789v2_fill_rect(...)`
- `esp_st7789v2_fill_round_rect(...)`
- `esp_st7789v2_fill_screen(...)`
- `esp_st7789v2_draw_char(...)`
- `esp_st7789v2_draw_text(...)`
- `esp_st7789v2_draw_text_aligned(...)`
