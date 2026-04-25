#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#include <dtcore/dtbuffer.h>
#include <dtcore/dtbytes.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtraster.h>
#include <dtcore/dtraster_rgb565.h>
#include <dtcore/dtrgb565.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtmc_base/dtdisplay.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtdisplay_lcd_ili9341.h>
#include <dtmc/dtmc_espidf.h>

DTDISPLAY_INIT_VTABLE(dtdisplay_lcd_ili9341);
DTOBJECT_INIT_VTABLE(dtdisplay_lcd_ili9341);

#define TAG "dtdisplay_lcd_ili9341"

// the implementation
typedef struct dtdisplay_lcd_ili9341_t
{
    DTDISPLAY_COMMON_MEMBERS;
    dtdisplay_lcd_ili9341_config_t configuration;

    dtbuffer_t* backing_buffer;
    dtraster_handle backing_raster;

    uint32_t dirty_event_type;

    dttasker_handle event_loop_tasker_handle;
    dtruntime_milliseconds_t event_loop_poll_ms;

    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;

    bool is_malloced;
    bool is_lcd_initialized;
    bool event_loop_should_quit;

} dtdisplay_lcd_ili9341_t;

// forward declarations
static dterr_t*
dtdisplay_lcd_ili9341__event_loop_entrypoint(void* self_, dttasker_handle tasker_handle);

// --------------------------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__setup(dtdisplay_lcd_ili9341_t* self DTDISPLAY_ATTACH_ARGS);
static dterr_t*
dtdisplay_lcd_ili9341__teardown(dtdisplay_lcd_ili9341_t* self DTDISPLAY_DETACH_ARGS);
static dterr_t*
dtdisplay_lcd_ili9341__event_loop(dtdisplay_lcd_ili9341_t* self);
static dterr_t*
dtdisplay_lcd_ili9341__handle_event(dtdisplay_lcd_ili9341_t* self, int rc, void* event, bool* out_should_quit);
static dterr_t*
dtdisplay_lcd_ili9341__render_dirty(dtdisplay_lcd_ili9341_t* self);

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

static dterr_t*
cyd_backlight_on(void)
{
    dterr_t* dterr = NULL;
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_LCD_BL,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    DTMC_ESPIDF_C(gpio_config(&bk_gpio_config));
    DTMC_ESPIDF_C(gpio_set_level(PIN_NUM_LCD_BL, 1));
cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_create(dtdisplay_lcd_ili9341_t** self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtdisplay_lcd_ili9341_t), "dtdisplay_lcd_ili9341_t", (void**)self));
    DTERR_C(dtdisplay_lcd_ili9341_init(*self));

    (*self)->is_malloced = true;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
static bool vtables_are_registered = false;

dterr_t*
dtdisplay_lcd_ili9341_register_vtables(void)
{
    dterr_t* dterr = NULL;

    if (!vtables_are_registered)
    {
        int32_t model_number = DTMC_BASE_CONSTANTS_DISPLAY_MODEL_ESPIDF_ILI9341;

        DTERR_C(dtdisplay_set_vtable(model_number, &dtdisplay_lcd_ili9341_display_vt));
        DTERR_C(dtobject_set_vtable(model_number, &dtdisplay_lcd_ili9341_object_vt));

        vtables_are_registered = true;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_init(dtdisplay_lcd_ili9341_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_DISPLAY_MODEL_ESPIDF_ILI9341;

    DTERR_C(dtdisplay_lcd_ili9341_register_vtables());

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_config(dtdisplay_lcd_ili9341_t* self, const dtdisplay_lcd_ili9341_config_t* configuration)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(configuration);

    if (self->backing_buffer != NULL || self->backing_raster != NULL)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot configure after configuring once");
        goto cleanup;
    }

    self->configuration = *configuration;

    if (self->configuration.window_width <= 0 || //
        self->configuration.window_height <= 0)
    {
        dterr = dterr_new(DTERR_BADCONFIG,
          DTERR_LOC,
          NULL,
          "window size [%" PRIu32 "x%" PRIu32 "] is invalid",
          self->configuration.window_width,
          self->configuration.window_height);
        goto cleanup;
    }

    if (self->configuration.title[0] == '\0')
    {
        snprintf(self->configuration.title,
          sizeof(self->configuration.title),
          "dtdisplay_lcd_ili9341 [%" PRIu32 "x%" PRIu32 "]",
          self->configuration.window_width,
          self->configuration.window_height);
    }

    // raster organizes the buffer and provides pixel access
    int32_t stride_bytes = self->configuration.window_width * (int32_t)sizeof(dtrgb565_t);
    {
        dtraster_rgb565_t* o;
        DTERR_C(dtraster_rgb565_create(&o));
        self->backing_raster = (dtraster_handle)o;
        dtraster_rgb565_config_t c = {
            .w = self->configuration.window_width,
            .h = self->configuration.window_height,
            .stride_bytes = stride_bytes,
            .should_store_as_big_endian = false,
        };
        DTERR_C(dtraster_rgb565_config(o, &c));
    }

    // buffer holds the raw pixels
    DTERR_C(dtbuffer_create(&self->backing_buffer, self->configuration.window_height * stride_bytes));
    memset(self->backing_buffer->payload, 0, (size_t)self->backing_buffer->length);

    DTERR_C(dtraster_use_buffer(self->backing_raster, self->backing_buffer));
    dtlog_debug(TAG,
      "configuring backing buffer payload %p with length %" PRIu32,
      self->backing_buffer->payload,
      self->backing_buffer->length);

    // how long the event loop waits for events before timing out and checking if it should quit
    self->event_loop_poll_ms = 20;

    // configure event loop task
    {
        dttasker_config_t c = { 0 };
        c.name = "display";
        c.tasker_entry_point_fn = dtdisplay_lcd_ili9341__event_loop_entrypoint;
        c.tasker_entry_point_arg = self;
        c.stack_size = 4096;
        c.priority = DTTASKER_PRIORITY_NORMAL_LOWEST;
        c.core = 0;
        DTERR_C(dttasker_create(&self->event_loop_tasker_handle, &c));
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
// create a raster of the same type as the backing raster
// this is used by clients who want to create a raster compatible with the display for blitting
dterr_t*
dtdisplay_lcd_ili9341_create_compatible_raster(dtdisplay_lcd_ili9341_t* self DTRASTER_CREATE_COMPATIBLE_RASTER_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_raster_handle);

    dtraster_rgb565_t* o;
    DTERR_C(dtraster_rgb565_create(&o));
    *out_raster_handle = (dtraster_handle)o;
    dtraster_rgb565_config_t c = {
        .w = w,
        .h = h,
        .stride_bytes = w * sizeof(dtrgb565_t),
        .should_store_as_big_endian = true,
    };
    DTERR_C(dtraster_rgb565_config(o, &c));

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_attach(dtdisplay_lcd_ili9341_t* self DTDISPLAY_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (self->is_lcd_initialized)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot attach after attaching once");
        goto cleanup;
    }
    // start event loop task
    DTERR_C(dttasker_start(self->event_loop_tasker_handle));

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_detach(dtdisplay_lcd_ili9341_t* self DTDISPLAY_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->event_loop_should_quit = true;
    dtruntime_sleep_milliseconds(self->event_loop_poll_ms * 2); // give the event loop some time to quit

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341_blit(dtdisplay_lcd_ili9341_t* self DTDISPLAY_BLIT_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(raster_handle);

    // blit the incoming raster to the backing raster
    DTERR_C(dtraster_blit(self->backing_raster, raster_handle, x, y));

    DTERR_C(dtdisplay_lcd_ili9341__render_dirty(self));

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------
void
dtdisplay_lcd_ili9341_copy(dtdisplay_lcd_ili9341_t* self, dtdisplay_lcd_ili9341_t* that_handle)
{
    // no fields to copy
}

// --------------------------------------------------------------------------------------------
void
dtdisplay_lcd_ili9341_dispose(dtdisplay_lcd_ili9341_t* self)
{
    if (self == NULL)
        return;

    dtraster_dispose(self->backing_raster);
    dtbuffer_dispose(self->backing_buffer);

    bool is_malloced = self->is_malloced;
    memset(self, 0, sizeof(*self));

    if (is_malloced)
        dtheaper_free(self);
}

// --------------------------------------------------------------------------------------------
bool
dtdisplay_lcd_ili9341_equals(dtdisplay_lcd_ili9341_t* a, dtdisplay_lcd_ili9341_t* b)
{
    return false;
}

// --------------------------------------------------------------------------------------------
const char*
dtdisplay_lcd_ili9341_get_class(dtdisplay_lcd_ili9341_t* self)
{
    (void)self;
    return "dtdisplay_lcd_ili9341_t";
}

// --------------------------------------------------------------------------------------------
bool
dtdisplay_lcd_ili9341_is_iface(dtdisplay_lcd_ili9341_t* self, const char* iface_name)
{
    (void)self;

    if (iface_name == NULL)
        return false;

    return strcmp(iface_name, "dtdisplay_iface") == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
void
dtdisplay_lcd_ili9341_to_string(dtdisplay_lcd_ili9341_t* self, char* buffer, size_t buffer_size)
{
    snprintf(buffer,
      buffer_size,
      "dtdisplay_lcd_ili9341 \"%s\" [%" PRIu32 "x%" PRIu32 "]",
      self->configuration.title,
      self->configuration.window_width,
      self->configuration.window_height);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
dterr_t*
dtdisplay_lcd_ili9341__event_loop_entrypoint(void* self_, dttasker_handle tasker_handle)
{
    dtdisplay_lcd_ili9341_t* self = (dtdisplay_lcd_ili9341_t*)self_;
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(tasker_handle);

    // set up the LCD environment and related resources
    DTERR_C(dtdisplay_lcd_ili9341__setup(self));

    // signal caller that the event loop is ready
    DTERR_C(dttasker_ready(tasker_handle));

    // run the event loop until it signals to quit
    DTERR_C(dtdisplay_lcd_ili9341__event_loop(self));

cleanup:
    if (dterr)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "rx task exiting");
    }

    // if configured, notify the listener that the event loop is exiting
    if (self->configuration.join_semaphore)
    {
        dterr_t* dterr_semaphore = dtsemaphore_post(self->configuration.join_semaphore);
        if (dterr_semaphore)
        {
            dterr_append(dterr, dterr_semaphore);
        }
    }

    // tear down the LCD environment and related resources
    {
        dterr_t* dterr_teardown = dtdisplay_lcd_ili9341__teardown(self);
        if (dterr_teardown)
        {
            dterr_append(dterr, dterr_teardown);
        }
    }

    return dterr;
}

// --------------------------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__setup(dtdisplay_lcd_ili9341_t* self DTDISPLAY_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (self->is_lcd_initialized)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot setup after setting up once");
        goto cleanup;
    }

    dtlog_debug(TAG, "initializing backlight");
    DTERR_C(cyd_backlight_on());

    dtlog_debug(TAG, "initializing SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_LCD_SCLK,
        .mosi_io_num = PIN_NUM_LCD_MOSI,
        .miso_io_num = PIN_NUM_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    DTMC_ESPIDF_C(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    dtlog_debug(TAG, "installing panel IO");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    DTMC_ESPIDF_C(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &self->io_handle));

    dtlog_debug(TAG, "installing ILI9341 panel driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    DTMC_ESPIDF_C(esp_lcd_new_panel_ili9341(self->io_handle, &panel_config, &self->panel_handle));

    DTMC_ESPIDF_C(esp_lcd_panel_reset(self->panel_handle));
    DTMC_ESPIDF_C(esp_lcd_panel_init(self->panel_handle));
    // DTMC_ESPIDF_C(esp_lcd_panel_invert_color(self->panel_handle, true));
    DTMC_ESPIDF_C(esp_lcd_panel_disp_on_off(self->panel_handle, true));
    DTMC_ESPIDF_C(esp_lcd_panel_swap_xy(self->panel_handle, true));
    // DTMC_ESPIDF_C(esp_lcd_panel_mirror(self->panel_handle, true, false));

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__teardown(dtdisplay_lcd_ili9341_t* self DTDISPLAY_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__event_loop(dtdisplay_lcd_ili9341_t* self)
{
    dterr_t* dterr = NULL;

    void* event = NULL;
    int rc = 0;
    bool out_should_quit = false;

    while (true)
    {
        dtruntime_sleep_milliseconds(self->event_loop_poll_ms);

        DTERR_C(dtdisplay_lcd_ili9341__handle_event(self, rc, event, &out_should_quit));
        if (out_should_quit)
            break;
    }

cleanup:
    if (dterr)
    {
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "event loop exiting due to error");
    }
    else
    {
        dtlog_debug(TAG,
          "event loop exiting normally on because %s",
          self->event_loop_should_quit ? "commanded event_loop_should_quit"
          : out_should_quit            ? "LCD mechanism quit"
                                       : "unknown");
    }

    return dterr;
}

// --------------------------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__handle_event(dtdisplay_lcd_ili9341_t* self, int rc, void* event, bool* out_should_quit)
{
    dterr_t* dterr = NULL;

    *out_should_quit = false;

    goto cleanup;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
static dterr_t*
dtdisplay_lcd_ili9341__render_dirty(dtdisplay_lcd_ili9341_t* self)
{
    dterr_t* dterr = NULL;

    // for now, blit the whole backing raster to the display on every blit call

    {
        char s[256];
        dtbytes_compose_hex(self->backing_buffer->payload, 32, s, sizeof(s));
        dtlog_debug(TAG, "%s is the dirty backing buffer", s);
    }

    DTMC_ESPIDF_C(esp_lcd_panel_draw_bitmap(self->panel_handle,
      0,
      0,
      self->configuration.window_width,
      self->configuration.window_height,
      (void*)self->backing_buffer->payload));

    goto cleanup;

cleanup:
    return dterr;
}
