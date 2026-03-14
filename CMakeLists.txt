idf_component_register(
    SRCS "esp_st7789v2.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_system
)

