# esp_st7789v2 examples

## Inicializacao minima

```c
ESP_ERROR_CHECK(esp_at_init(false));
ESP_ERROR_CHECK(esp_st7789v2_init(false, true));
ESP_ERROR_CHECK(esp_st7789v2_set_rotation(ESP_ST7789V2_ROTATION_0));
ESP_ERROR_CHECK(esp_st7789v2_fill_screen(BLACK));
```

## Geometrias basicas

```c
ESP_ERROR_CHECK(esp_st7789v2_draw_pixel(0, 0, WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_line(0, 0, 319, 169, WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_rect(10, 10, 80, 40, RED));
ESP_ERROR_CHECK(esp_st7789v2_fill_rect(20, 20, 40, 20, GREEN));
ESP_ERROR_CHECK(esp_st7789v2_draw_round_rect(120, 30, 80, 40, 8, ORANGE));
ESP_ERROR_CHECK(esp_st7789v2_fill_round_rect(210, 30, 80, 40, 10, TEAL));
ESP_ERROR_CHECK(esp_st7789v2_draw_circle(80, 120, 20, MAGENTA));
ESP_ERROR_CHECK(esp_st7789v2_fill_circle(150, 120, 18, BLUE));
ESP_ERROR_CHECK(esp_st7789v2_draw_triangle(220, 140, 260, 90, 300, 140, WHITE));
```

## Grade

```c
ESP_ERROR_CHECK(esp_st7789v2_fill_screen(BLACK));
ESP_ERROR_CHECK(esp_st7789v2_draw_rect(0, 0, esp_st7789v2_width(), esp_st7789v2_height(), WHITE));
ESP_ERROR_CHECK(esp_st7789v2_draw_grid(10, 10, 300, 150, 10, 5, DARK_GRAY));
```

## Texto bitmap

```c
ESP_ERROR_CHECK(esp_st7789v2_draw_text(10, 10, "HELLO", WHITE, BLACK, 2));
ESP_ERROR_CHECK(esp_st7789v2_draw_text_aligned(40, "CENTER", YELLOW, BLACK, 2, ESP_ST7789V2_ALIGN_CENTER));
ESP_ERROR_CHECK(esp_st7789v2_draw_text_box(10, 70, 140, 24, "TEMP 25.4", WHITE, BLACK, 2, ESP_ST7789V2_ALIGN_LEFT));
```

## Atualizacao por setor

```c
static esp_st7789v2_text_box_t s_temp_box = {
    .x = 10,
    .y = 10,
    .width = 140,
    .height = 24,
    .scale = 2,
    .fg = WHITE,
    .bg = BLACK,
    .align = ESP_ST7789V2_ALIGN_LEFT,
    .initialized = true,
};

ESP_ERROR_CHECK(esp_st7789v2_update_text_box_if_changed(&s_temp_box, "TEMP 25.4"));
ESP_ERROR_CHECK(esp_st7789v2_update_text_box_if_changed(&s_temp_box, "TEMP 25.7"));
```

## 7 segmentos

```c
ESP_ERROR_CHECK(esp_st7789v2_draw_7seg_text(10, 10, "12:34", 48, 8, WHITE, BLACK));
ESP_ERROR_CHECK(esp_st7789v2_draw_7seg_text(10, 70, "25°C", 42, 6, YELLOW, BLACK));
```

## Barra de progresso

```c
static esp_st7789v2_progress_bar_t s_bar = {
    .x = 10,
    .y = 120,
    .width = 200,
    .height = 20,
    .min = 0,
    .max = 100,
    .value = 0,
    .border_color = WHITE,
    .fill_color = GREEN,
    .bg_color = BLACK,
    .initialized = true,
};

ESP_ERROR_CHECK(esp_st7789v2_draw_progress_bar(10, 120, 200, 20, 0, 100, 75, WHITE, GREEN, BLACK));
ESP_ERROR_CHECK(esp_st7789v2_update_progress_bar_if_changed(&s_bar, 80));
```
