# esp_st7789v2

Driver local para ST7789 170x320 baseado em `esp_lcd`, calibrado no hardware do projeto.

Objetivo:
- bring-up rapido do painel
- API direta para desenho
- atualizacao por setor, sem limpar a tela inteira
- comandos AT opcionais para validacao em bancada

## Estado atual

Base validada:
- orientacao e origem visual corretas
- `clear`, pixel e retangulos estaveis
- paleta RGB565 calibrada no painel real
- atualizacao por area sem full redraw

Primitivas disponiveis:
- pixel
- linha arbitraria
- hline / vline
- rect / fill rect
- round rect / fill round rect
- circle / fill circle
- triangle
- grid

Texto:
- fonte bitmap `5x7`
- alinhamento `LEFT`, `CENTER`, `RIGHT`
- `text box` com limpeza local
- `7seg` para numeros e pontuacao

Widgets simples:
- `text box`
- `progress bar`

## API

Inicializacao:
- `esp_st7789v2_init(log_enabled, at_enabled)`
- `esp_st7789v2_deinit()`
- `esp_st7789v2_is_initialized()`

Painel:
- `esp_st7789v2_set_rotation(...)`
- `esp_st7789v2_set_invert(...)`
- `esp_st7789v2_width()`
- `esp_st7789v2_height()`

Geometria:
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
- `esp_st7789v2_fill_round_rect(...)`
- `esp_st7789v2_fill_rect(...)`
- `esp_st7789v2_fill_screen(...)`
- `esp_st7789v2_clear_area(...)`

Texto:
- `esp_st7789v2_draw_char(...)`
- `esp_st7789v2_draw_text(...)`
- `esp_st7789v2_draw_text_aligned(...)`
- `esp_st7789v2_draw_text_box(...)`
- `esp_st7789v2_update_text_box_if_changed(...)`
- `esp_st7789v2_text_width(...)`

7 segmentos:
- `esp_st7789v2_draw_7seg_char(...)`
- `esp_st7789v2_draw_7seg_text(...)`
- `esp_st7789v2_7seg_text_width(...)`

Barra de progresso:
- `esp_st7789v2_draw_progress_bar(...)`
- `esp_st7789v2_update_progress_bar_if_changed(...)`

## Atualizacao por setor

O caminho certo para evitar flicker e:
- nao usar `fill_screen(...)` em toda mudanca
- limpar so a area que mudou
- redesenhar so o setor alterado

Helpers para isso:
- `esp_st7789v2_clear_area(...)`
- `esp_st7789v2_draw_text_box(...)`
- `esp_st7789v2_update_text_box_if_changed(...)`
- `esp_st7789v2_draw_progress_bar(...)`
- `esp_st7789v2_update_progress_bar_if_changed(...)`

## Comandos AT

Se `esp_st7789v2_init(log_enabled, true)` for usado, a lib registra comandos AT de validacao.

Lista completa em:
- `COMMANDS.md`

## Exemplos

Exemplos de uso:
- `EXAMPLES.md`
