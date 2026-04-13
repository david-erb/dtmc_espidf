// dtsemaphore_espidf.c
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dttimeout.h>

#include <dtmc_base/dtsemaphore.h>

#define TAG "dtsemaphore_espidf"

// --------------------------------------------------------------------------------------
typedef struct dtsemaphore_espidf_t
{
    uint32_t count;     // current semaphore count
    uint32_t max_count; // 0 means "no explicit cap" (use INT32_MAX)

    SemaphoreHandle_t semaphore;

    bool is_semaphore_initialized;
} dtsemaphore_espidf_t;

// --------------------------------------------------------------------------------------
extern dterr_t*
dtsemaphore_create(dtsemaphore_handle* self_handle, int32_t initial_count, int32_t max_count)
{
    dterr_t* dterr = NULL;
    dtsemaphore_espidf_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(self_handle);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtsemaphore_espidf_t), "dtsemaphore_espidf_t", (void**)&self));

    self->max_count = (max_count == 0) ? INT32_MAX : max_count;
    self->count = initial_count;

    // FreeRTOS counting semaphore
    self->semaphore = xSemaphoreCreateCounting((UBaseType_t)self->max_count, (UBaseType_t)self->count);
    if (self->semaphore == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "xSemaphoreCreateCounting failed");
        goto cleanup;
    }

    self->is_semaphore_initialized = true;

    *self_handle = (dtsemaphore_handle)self;

cleanup:
    if (dterr != NULL)
    {
        dtsemaphore_dispose((dtsemaphore_handle)self);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtsemaphore_post(dtsemaphore_handle self_handle)
{
    dterr_t* dterr = NULL;
    dtsemaphore_espidf_t* self = (dtsemaphore_espidf_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->semaphore);

    if (!self->is_semaphore_initialized)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "semaphore not initialized");
        goto cleanup;
    }

    // Note: this is the task-context variant. If you need ISR support, add a *_from_isr path.
    BaseType_t ok = xSemaphoreGive(self->semaphore);
    if (ok != pdTRUE)
    {
        // Typically means it was already at max count.
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "semaphore give failed (likely full)");
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
static inline TickType_t
_dt_ms_to_ticks(int32_t timeout_ms)
{
    if (timeout_ms == DTTIMEOUT_NOWAIT)
        return 0;
    if (timeout_ms == DTTIMEOUT_FOREVER)
        return portMAX_DELAY;
    if (timeout_ms < 0)
        return 0; // defensive
    return pdMS_TO_TICKS((uint32_t)timeout_ms);
}

// --------------------------------------------------------------------------------------

dterr_t*
dtsemaphore_wait(dtsemaphore_handle self_handle, dttimeout_millis_t timeout_milliseconds, bool* was_timeout)
{
    dterr_t* dterr = NULL;
    dtsemaphore_espidf_t* self = (dtsemaphore_espidf_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->semaphore);

    if (!self->is_semaphore_initialized)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "semaphore not initialized");
        goto cleanup;
    }

    TickType_t ticks = _dt_ms_to_ticks(timeout_milliseconds);

    // portMAX_DELAY blocks forever (requires INCLUDE_vTaskSuspend=1, true in ESP-IDF)
    BaseType_t ok = xSemaphoreTake(self->semaphore, ticks);
    if (ok != pdTRUE)
    {
        if (timeout_milliseconds == DTTIMEOUT_NOWAIT || timeout_milliseconds >= 0)
        {
            if (was_timeout != NULL)
                *was_timeout = true;
            else
                dterr = dterr_new(DTERR_TIMEOUT,
                  DTERR_LOC,
                  NULL,
                  "semaphore wait timed out after %" DTTIMEOUT_MILLIS_PRI " ms",
                  timeout_milliseconds);
        }
        else
        {
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "semaphore wait failed");
        }
        goto cleanup;
    }
    if (was_timeout != NULL)
        *was_timeout = false;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// best-effort teardown (OK even if never initialized beyond zeroed memory)
void
dtsemaphore_dispose(dtsemaphore_handle self_handle)
{
    dtsemaphore_espidf_t* self = NULL;
    self = (dtsemaphore_espidf_t*)self_handle;

    if (self == NULL)
        return;

    if (self->semaphore)
    {
        vSemaphoreDelete(self->semaphore);
        self->semaphore = NULL;
    }

    self->is_semaphore_initialized = false;

    dtheaper_free(self);
}
