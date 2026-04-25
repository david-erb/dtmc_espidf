#include <stdlib.h>
#include <string.h>

#include <driver/spi_master.h>
#include <esp_heap_caps.h>

#include <esp_err.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtdotstar.h>

#include <dtmc/dtdotstar_espidf.h>
#include <dtmc/dtmc_espidf.h>

// 5MHz SPI speed
#define DOTSTAR_SPI_SPEED_HZ 5000000
// brightness value for the DotStar LEDs (0-31)
// #define DOTSTAR_BRIGHTNESS 15

DTDOTSTAR_INIT_VTABLE(dtdotstar_espidf);

#define TAG "dtdotstar_espidf"

#define dtlog_debug(...)

// --------------------------------------------------------------------------------------
extern dterr_t*
dtdotstar_espidf_create(dtdotstar_espidf_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtdotstar_espidf_t*)malloc(sizeof(dtdotstar_espidf_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %zu bytes for dtdotstar_espidf_t", sizeof(dtdotstar_espidf_t));
        goto cleanup;
    }

    DTERR_C(dtdotstar_espidf_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtdotstar_dummy_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_init(dtdotstar_espidf_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_DOTSTAR_MODEL_ESPIDF;

    // register the vtable for this model number
    DTERR_C(dtdotstar_set_vtable(self->model_number, &dtdotstar_espidf_vt));

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtdotstar_espidf_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_configure(dtdotstar_espidf_t* self, dtdotstar_espidf_config_t* config)
{
    self->config = *config;
    return NULL; // success
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_set_post_cb(dtdotstar_espidf_t* self, dtdotstar_post_cb_fn post_cb, void* user_context)
{
    self->_post_cb = post_cb;
    self->_post_cb_user_context = user_context;
    return NULL;
}

// --------------------------------------------------------------------------------------
static void IRAM_ATTR
_post_cb(spi_transaction_t* t)
{
    if (t == NULL)
        return;

    if (t->user == NULL)
        return;

    dtdotstar_espidf_t* self = (dtdotstar_espidf_t*)t->user;

    if (self->_post_cb == NULL)
        return;

    self->_post_cb(self->_post_cb_user_context);
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_connect(dtdotstar_espidf_t* self)
{
    dterr_t* dterr = NULL;

    int led_count = self->config.led_count;

    int start_frame_size = 4;
    int led_frames_size = led_count * 4;
    int end_frame_size = (led_count + 15) / 16 + 8;

    dtlog_debug(TAG,
      "dtdotstar_espidf_connect: led_count=%d start_frame_size=%d led_frames_size=%d end_frame_size=%d total=%d",
      led_count,
      start_frame_size,
      led_frames_size,
      end_frame_size,
      start_frame_size + led_frames_size + end_frame_size);

    self->spi_buffer_size = start_frame_size + led_frames_size + end_frame_size;
    self->spi_buffer = heap_caps_malloc(self->spi_buffer_size, MALLOC_CAP_DMA);
    if (!self->spi_buffer)
    {
        dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %d bytes for spi buffer", self->spi_buffer_size);
        goto cleanup;
    }

    // clear the start frame
    memset(self->spi_buffer, 0, start_frame_size);
    // set the end frame to 0xFF
    memset(&self->spi_buffer[start_frame_size + led_frames_size], 0xFF, end_frame_size);

    spi_bus_config_t buscfg = {
        .mosi_io_num = self->config.mosi_io,
        .miso_io_num = -1,
        .sclk_io_num = self->config.sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = self->spi_buffer_size,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = DOTSTAR_SPI_SPEED_HZ, .mode = 0, .spics_io_num = -1, .queue_size = 1, .post_cb = _post_cb
    };

    // When spi_bus_initialize returns ESP_ERR_INVALID_STATE that means meaning it's already initialized.
    esp_err_t esp_err;
    esp_err = spi_bus_initialize(self->config.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (esp_err == ESP_ERR_INVALID_STATE)
    {
    }
    else if (esp_err != ESP_OK)
    {
        dterr = dterr_new(esp_err, DTERR_LOC, NULL, "spi_bus_initialize error %s", esp_err_to_name(esp_err));
        goto cleanup;
    }

    esp_err = spi_bus_add_device(self->config.spi_host, &devcfg, &self->spi_handle);
    if (esp_err != ESP_OK)
    {
        dterr = dterr_new(esp_err, DTERR_LOC, NULL, "spi_bus_add_device error %s", esp_err_to_name(esp_err));
        goto cleanup;
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtdotstar_espidf_connect failed");
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_dither(dtdotstar_espidf_t* self DTDOTSTAR_DITHER_ARGS)
{
    dterr_t* dterr = NULL;

    int offset = 4; // skip start frame
    for (int i = 0; i < self->config.led_count; i++)
    {
        self->spi_buffer[offset++] = 0b11100000 | ((uint8_t)lumens[i].Flux & 0x1F); // global brightness
        self->spi_buffer[offset++] = (uint8_t)lumens[i].B;
        self->spi_buffer[offset++] = (uint8_t)lumens[i].G;
        self->spi_buffer[offset++] = (uint8_t)lumens[i].R;
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_transmit(dtdotstar_espidf_t* self DTDOTSTAR_TRANSMIT_ARGS)
{
    dterr_t* dterr = NULL;

    self->spi_transaction.length = self->spi_buffer_size * 8;
    self->spi_transaction.tx_buffer = self->spi_buffer;

    DTMC_ESPIDF_C(spi_device_transmit(self->spi_handle, &self->spi_transaction))

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtdotstar_espidf_enqueue(dtdotstar_espidf_t* self DTDOTSTAR_ENQUEUE_ARGS)
{
    dterr_t* dterr = NULL;

    self->spi_transaction.length = self->spi_buffer_size * 8;
    self->spi_transaction.tx_buffer = self->spi_buffer;
    self->spi_transaction.user = self;

    DTMC_ESPIDF_C(spi_device_queue_trans(self->spi_handle, &self->spi_transaction, 0))

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtdotstar_espidf_dispose(dtdotstar_espidf_t* self)
{
    if (self->spi_buffer)
    {
        free(self->spi_buffer);
        self->spi_buffer = NULL;
        self->spi_buffer_size = 0;
    }

    if (self->spi_handle)
    {
        // TODO: Handle removal of shared spi bus resource in dtdotstar_espidf_dispose.
        // Don't make this call if maybe other devices are using the spi bus.
        // spi_bus_remove_device(self->spi_handle);
        self->spi_handle = NULL;
    }
}
