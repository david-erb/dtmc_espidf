/*
 * dtdotstar_espidf -- ESP-IDF SPI backend for the dtdotstar APA102 LED strip interface.
 *
 * Implements the dtdotstar vtable using the ESP-IDF SPI master driver,
 * driving an APA102/DotStar addressable LED strip over a configurable SPI
 * host and GPIO pins. The LED count, MOSI and clock pins, and SPI host are
 * set at configuration time. An optional post callback supports async
 * completion notification after each transmitted frame.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <driver/spi_master.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtdotstar.h>

typedef struct dtdotstar_espidf_config_t
{
    int led_count;
    int mosi_io;
    int sclk_io;
    spi_host_device_t spi_host;
} dtdotstar_espidf_config_t;

// TODO: make dtdotstar_espidf_t opaque.
typedef struct dtdotstar_espidf_t
{
    DTDOTSTAR_COMMON_MEMBERS;

    // data members
    dtdotstar_espidf_config_t config;
    dtdotstar_post_cb_fn _post_cb;
    void* _post_cb_user_context;
    spi_device_handle_t spi_handle;
    spi_transaction_t spi_transaction;
    uint8_t* spi_buffer;
    int spi_buffer_size;
    bool _is_malloced;
} dtdotstar_espidf_t;

extern dterr_t*
dtdotstar_espidf_create(dtdotstar_espidf_t** self_ptr);

extern dterr_t*
dtdotstar_espidf_init(dtdotstar_espidf_t* instance);

extern dterr_t*
dtdotstar_espidf_configure(dtdotstar_espidf_t* self, dtdotstar_espidf_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTDOTSTAR_DECLARE_API(dtdotstar_espidf);
