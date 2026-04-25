# demo_lvgl_card/src/CMakeLists.txt

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        dtcore_library
        dtmc_base_library
        dtmc_base_demos
        dtmc_espidf_library_lcd_panel
        dtmc_espidf_library
        esp_lcd_ili9341
        esp_timer
)