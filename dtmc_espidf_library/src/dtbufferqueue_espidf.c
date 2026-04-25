#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dtbytes.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtledger.h>
#include <dtcore/dtlog.h>
#include <dtcore/dttimeout.h>

#include <dtmc_base/dtbufferqueue.h>

#define TAG "dtbufferqueue"
#define dtlog_debug(TAG, ...)

// --------------------------------------------------------------------------------------
// Types

typedef struct dtbufferqueue_t
{
    int32_t max_count;
    bool should_overwrite;

    QueueHandle_t queue;

    bool is_queue_initialized;

} dtbufferqueue_t;

// --------------------------------------------------------------------------------------
// Helpers

static inline TickType_t
ms_to_ticks_forever(int timeout_millis)
{
    if (timeout_millis == DTTIMEOUT_FOREVER)
        return portMAX_DELAY;
    if (timeout_millis <= 0)
        return 0;
    return pdMS_TO_TICKS((uint32_t)timeout_millis);
}

static inline dterr_t*
freertos_timeout_err(const char* where, int timeout_millis)
{
    return dterr_new(DTERR_TIMEOUT, DTERR_LOC, NULL, "%s timed out after %d ms", where, timeout_millis);
}

// --------------------------------------------------------------------------------------
extern dterr_t*
dtbufferqueue_create(dtbufferqueue_handle* out_handle, int32_t max_count, bool should_overwrite)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(out_handle);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtbufferqueue_t), "dtbufferqueue_t", (void**)&self));

    self->max_count = max_count;
    self->should_overwrite = should_overwrite;

    if (self->max_count == 0)
        self->max_count = 1;

    // Each element is a dtbuffer_t* (pointer) copied into the queue
    self->queue = xQueueCreate((UBaseType_t)self->max_count, (UBaseType_t)sizeof(dtbuffer_t*));
    if (self->queue == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "xQueueCreate failed (len=%d, item_size=%zu)", self->max_count, sizeof(dtbuffer_t*));
        goto cleanup;
    }

    self->is_queue_initialized = true;

    *out_handle = (dtbufferqueue_handle)self;

cleanup:
    if (dterr != NULL)
        dtbufferqueue_dispose((dtbufferqueue_handle)self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtbufferqueue_put(dtbufferqueue_handle handle DTBUFFERQUEUE_PUT_ARGS)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    TickType_t ticks = ms_to_ticks_forever((int)timeout_millis);

#ifndef dtlog_debug
    {
        char tmp[64];
        dtbytes_compose_hex((const char*)buffer->payload, buffer->length, tmp, sizeof(tmp));
        dtlog_debug(TAG, "xQueueSend pushing buffer %p length %" PRId32 " %s", buffer, buffer->length, tmp);
    }
#endif

    BaseType_t ok = xQueueSend(self->queue, &buffer, ticks);
    if (ok != pdPASS)
    {
        // Only realistic failure in task context is timeout/full
        // dterr = freertos_timeout_err("xQueueSend", (int)timeout_millis);
        if (was_timeout != NULL)
            *was_timeout = true;

        goto cleanup;
    }
    else
    {
        if (was_timeout != NULL)
            *was_timeout = false;
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to enqueue buffer");
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtbufferqueue_get(dtbufferqueue_handle handle DTBUFFERQUEUE_GET_ARGS)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    TickType_t ticks = ms_to_ticks_forever((int)timeout_millis);

    dtbuffer_t* out = NULL;
    BaseType_t ok = xQueueReceive(self->queue, &out, ticks);
    if (ok != pdPASS)
    {
        // dterr = freertos_timeout_err("xQueueReceive", (int)timeout_millis);
        if (was_timeout != NULL)
            *was_timeout = true;
        goto cleanup;
    }
    else
    {
        if (was_timeout != NULL)
            *was_timeout = false;
    }

    *buffer = out;

#ifndef dtlog_debug
    {
        char tmp[64];
        dtbytes_compose_hex((const char*)(*buffer)->payload, (*buffer)->length, tmp, sizeof(tmp));
        dtlog_debug(TAG, "xQueueReceive returned buffer %p length %" PRId32 " %s", *buffer, (*buffer)->length, tmp);
    }
#endif

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to dequeue buffer");
    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtbufferqueue_dispose(dtbufferqueue_handle handle)
{
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;
    {
        if (self == NULL)
            return;

        if (self->is_queue_initialized && self->queue != NULL)
        {
            // Free any remaining pointers in a predictable way (optional purge loop)
            dtbuffer_t* tmp = NULL;
            while (xQueueReceive(self->queue, &tmp, 0) == pdPASS)
            {
                // We do NOT own dtbuffer_t*; caller decides whether to free them.
                // This mirrors Zephyr's k_msgq_purge behavior (which drops contents).
            }
            vQueueDelete(self->queue);
            self->queue = NULL;
        }

        dtheaper_free(self);
    }
}