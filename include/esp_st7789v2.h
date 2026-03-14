#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_st7789v2 esp_st7789v2_t;

esp_err_t esp_st7789v2_init(void);
esp_err_t esp_st7789v2_deinit(void);

#ifdef __cplusplus
}
#endif
