#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>

#include <dtmc_base/dtlock.h>

#define TAG "dtlock_freertos"

// -------------------------------------------------------------------------------------------------
// Private implementation
typedef struct dtlock_espidf_t
{
    SemaphoreHandle_t mutex;
} dtlock_espidf_t;

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_create(dtlock_handle* self_handle)
{
    dterr_t* dterr = NULL;
    dtlock_espidf_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(self_handle);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtlock_espidf_t), "dtlock_espidf_t", (void**)&self));

    // Zephyr k_mutex is recursive. Match that here.
    self->mutex = xSemaphoreCreateRecursiveMutex();
    if (self->mutex == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "xSemaphoreCreateRecursiveMutex failed");
        goto cleanup;
    }

    *self_handle = (dtlock_handle)self;

cleanup:
    if (dterr != NULL)
    {
        dtlock_dispose((dtlock_handle)self);
    }
    return dterr;
}

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_acquire(dtlock_handle handle)
{
    dterr_t* dterr = NULL;
    dtlock_espidf_t* self = (dtlock_espidf_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->mutex);

    // NOTE: Do not call from ISR context.
    // (ESP-IDF provides xPortInIsrContext() in many targets; add a guard if needed)

    BaseType_t ok = xSemaphoreTakeRecursive(self->mutex, portMAX_DELAY);
    if (ok != pdTRUE)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "xSemaphoreTakeRecursive failed");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_release(dtlock_handle handle)
{
    dterr_t* dterr = NULL;
    dtlock_espidf_t* self = (dtlock_espidf_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->mutex);

    // Natural FreeRTOS behavior:
    // - if current task does not own the recursive mutex, give fails (pdFALSE)
    BaseType_t ok = xSemaphoreGiveRecursive(self->mutex);
    if (ok != pdTRUE)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "xSemaphoreGiveRecursive failed (not owner / not locked)");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------------------------
void
dtlock_dispose(dtlock_handle handle)
{
    dtlock_espidf_t* self = (dtlock_espidf_t*)handle;
    if (self == NULL)
        return;

    // Implies release (best-effort).
    if (self->mutex != NULL)
    {
        (void)xSemaphoreGiveRecursive(self->mutex);
        vSemaphoreDelete(self->mutex);
        self->mutex = NULL;
    }

    dtheaper_free(self);
}
