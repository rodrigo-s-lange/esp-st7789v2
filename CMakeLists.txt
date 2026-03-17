idf_component_register(
    SRCS "esp_st7789v2.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_spi esp_lcd freertos heap esp_at
)

