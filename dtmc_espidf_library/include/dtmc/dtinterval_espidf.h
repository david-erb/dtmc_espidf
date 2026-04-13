/*
 * dtinterval_espidf -- ESP-IDF FreeRTOS task backend for the dtinterval periodic timer interface.
 *
 * Implements the dtinterval vtable using a FreeRTOS task as the timing
 * source. The callback function, context pointer, task name, and interval
 * in microseconds are all set at configuration time. The callback receives
 * a should_pause flag, enabling self-cancellation without external
 * coordination.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>

#include <freertos/task.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtinterval.h>

typedef struct dtinterval_espidf_config_t
{
    const char* name;                   // name of the esp, used for logging and debugging
    dtinterval_callback_fn callback_fn; // function to call periodically
    void* callback_context;             // context to pass to the callback function
    TaskHandle_t periodic_task_handle;
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
