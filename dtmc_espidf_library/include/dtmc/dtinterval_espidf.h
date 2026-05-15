/*
 * dtinterval_espidf -- ESP-IDF FreeRTOS task backend for the dtinterval periodic timer interface.
 *
 * Implements the dtinterval vtable on ESP-IDF using esp_timer for periodic
 * wakeups. Configuration is limited to backend details such as timer name and
 * period; user callbacks are supplied through the dtinterval facade itself via
 * dtinterval_set_callback().
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtinterval.h>

typedef struct dtinterval_espidf_config_t
{
    const char* name; // name of the timer, used for logging and debugging
    int32_t periodic_interval_micros;

} dtinterval_espidf_config_t;

// TODO: make dtinterval_espidf_t opaque.
typedef struct dtinterval_espidf_t dtinterval_espidf_t;

extern dterr_t*
dtinterval_espidf_register_vtables();

extern dterr_t*
dtinterval_espidf_create(dtinterval_espidf_t** self_ptr);

extern dterr_t*
dtinterval_espidf_init(dtinterval_espidf_t* instance);

extern dterr_t*
dtinterval_espidf_configure(dtinterval_espidf_t* self, dtinterval_espidf_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTINTERVAL_DECLARE_API(dtinterval_espidf);
