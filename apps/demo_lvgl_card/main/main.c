#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>

#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lvgl_port.h>

#include <lvgl.h>

#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base_demos/demo_helpers.h>
#include <dtmc_base_demos/demo_lvgl_card.h>

#define TAG "main"

/*
    Common CYD / ESP32-2432S028R ILI9341 pins
    Display:
      MISO  = GPIO12
      MOSI  = GPIO13
      SCLK  = GPIO14
      CS    = GPIO15
      DC    = GPIO2
      RST   = -1
      BL    = GPIO21

    This matches the common pinout published for the ESP32-2432S028R.
*/

#define PIN_NUM_LCD_MISO 12
#define PIN_NUM_LCD_MOSI 13
#define PIN_NUM_LCD_SCLK 14
#define PIN_NUM_LCD_CS 15
#define PIN_NUM_LCD_DC 2
#define PIN_NUM_LCD_RST (-1)
#define PIN_NUM_LCD_BL 21

#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

static lv_disp_t* s_disp = NULL;

static void
cyd_backlight_on(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level(PIN_NUM_LCD_BL, 1));
}

static void
cyd_display_init(void)
{
    ESP_LOGI(TAG, "Initialize backlight");
    cyd_backlight_on();

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_LCD_SCLK,
        .mosi_io_num = PIN_NUM_LCD_MOSI,
        .miso_io_num = PIN_NUM_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ILI9341 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    dtlog_info(TAG, "resolution: %dx%d", LCD_H_RES, LCD_V_RES);

    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Initialize LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // ESP_LOGI(TAG, "Add display to LVGL");
    // const lvgl_port_display_cfg_t disp_cfg = {
    //     .io_handle = io_handle,
    //     .panel_handle = panel_handle,
    //     .buffer_size = LCD_H_RES * 40,
    //     .double_buffer = true,
    //     .hres = LCD_H_RES,
    //     .vres = LCD_V_RES,
    //     .monochrome = false,
    //     .rotation = {
    //         .swap_xy = false,
    //         .mirror_x = false,
    //         .mirror_y = false,
    //     },
    //     // .color_format = LV_COLOR_FORMAT_RGB565,
    // };

    ESP_LOGI(TAG, "Add display to LVGL");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_V_RES * 40,
        .double_buffer = true,
        .hres = LCD_V_RES,
        .vres = LCD_H_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = false,
        },
        // .color_format = LV_COLOR_FORMAT_RGB565,
    };

    s_disp = lvgl_port_add_disp(&disp_cfg);
    assert(s_disp != NULL);

    // // ESP_LOGI(TAG, "set rotation 90");
    // lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);

    // bool mirror_x = true;
    // bool mirror_y = true;
    // ESP_LOGI(TAG, "mirroring esp_lcd_panel: %s/%s", mirror_x ? "yes" : "no", mirror_y ? "yes" : "no");
    // ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, mirror_x, mirror_y));

    ESP_LOGI(TAG, "done with display initialization");
}

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;

    demo_t* demo = NULL;

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        DTERR_C(demo_configure(demo, &c));
    }

    // === initialize Linux-specific LVGL and its SDL drivers ===
    dtlog_info(TAG, "initializing cyd display");
    cyd_display_init();

    dtlog_info(TAG, "demo starting...");

    // === start the demo ===
    DTERR_C(demo_start(demo));

    dtlog_info(TAG, "demo started successfully");

    // === wait indefinitely to prevent the program from exiting
    while (1)
    {
        dtruntime_sleep_milliseconds(1000);
    }

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);
}
