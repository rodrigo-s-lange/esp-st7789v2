# esp_st7789v2 examples

Inicializacao minima:

```c
ESP_ERROR_CHECK(esp_at_init(false));
ESP_ERROR_CHECK(esp_st7789v2_init(false, true));
ESP_ERROR_CHECK(esp_st7789v2_set_rotation(ESP_ST7789V2_ROTATION_0));
ESP_ERROR_CHECK(esp_st7789v2_fill_screen(BLACK));
ESP_ERROR_CHECK(esp_st7789v2_draw_text_aligned(40, "HELLO", WHITE, BLACK, 2, ESP_ST7789V2_ALIGN_CENTER));
```

Grade de validacao:

```c
ESP_ERROR_CHECK(esp_st7789v2_fill_screen(BLACK));
ESP_ERROR_CHECK(esp_st7789v2_draw_rect(0, 0, esp_st7789v2_width(), esp_st7789v2_height(), WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_grid(10, 10, 300, 150, 10, 5, DARK_GRAY));
```

Primitivas:

```c
ESP_ERROR_CHECK(esp_st7789v2_draw_pixel(0, 0, WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_line(0, 0, 319, 169, WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_hline(10, 20, 100, YELLOW));
ESP_ERROR_CHECK(esp_st7789v2_draw_vline(20, 10, 80, CYAN));
ESP_ERROR_CHECK(esp_st7789v2_draw_rect(30, 30, 80, 40, RED));
ESP_ERROR_CHECK(esp_st7789v2_draw_round_rect(120, 30, 80, 40, 8, ORANGE));
ESP_ERROR_CHECK(esp_st7789v2_fill_rect(40, 40, 40, 20, GREEN));
ESP_ERROR_CHECK(esp_st7789v2_fill_round_rect(210, 30, 80, 40, 10, TEAL));
ESP_ERROR_CHECK(esp_st7789v2_draw_circle(80, 120, 20, MAGENTA));
ESP_ERROR_CHECK(esp_st7789v2_fill_circle(150, 120, 18, BLUE));
ESP_ERROR_CHECK(esp_st7789v2_draw_triangle(220, 140, 260, 90, 300, 140, WHITE));
```
