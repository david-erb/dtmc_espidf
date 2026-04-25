#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_timer.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtinterval.h>

#include <dtmc/dtinterval_espidf.h>
#include <dtmc/dtmc_espidf.h>

typedef struct dtinterval_espidf_t
{
    DTINTERVAL_COMMON_MEMBERS;
    dtinterval_espidf_config_t config; // configuration for this esp
    bool _is_malloced;                 // true if this instance was malloced, false if it was allocated on the stack

    esp_timer_handle_t periodic_timer;

} dtinterval_espidf_t;

DTINTERVAL_INIT_VTABLE(dtinterval_espidf);

#define TAG "dtinterval_espidf"
#define CLASS_NAME "dtinterval_espidf_t"

// --------------------------------------------------------------------------------------------
static bool vtables_are_registered = false;

dterr_t*
dtinterval_espidf_register_vtables(void)
{
    dterr_t* dterr = NULL;

    if (!vtables_are_registered)
    {
        int32_t model_number = DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ESPIDF; // Set correct model number

        // register the vtables for this model number
        DTERR_C(dtinterval_set_vtable(model_number, &dtinterval_espidf_vt));

        vtables_are_registered = true;
    }

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------
static IRAM_ATTR void
dtinterval_espidf_isr(void* arg)
{
    dtinterval_espidf_t* self = (dtinterval_espidf_t*)arg;

    if (self == NULL)
        return;

    if (self->config.periodic_task_handle == NULL)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(self->config.periodic_task_handle, &xHigherPriorityTaskWoken);

// Drop the portYIELD_FROM_ISR so we don't immediately jump into the task
#if 0
    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR(); // Switch immediately to the notified task
    }
#endif
}

// --------------------------------------------------------------------------------------
extern dterr_t*
dtinterval_espidf_create(dtinterval_espidf_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtinterval_espidf_t*)malloc(sizeof(dtinterval_espidf_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %zu bytes for %s", sizeof(dtinterval_espidf_t), CLASS_NAME);
        goto cleanup;
    }

    DTERR_C(dtinterval_espidf_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to create %s instance", CLASS_NAME);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtinterval_espidf_init(dtinterval_espidf_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ESPIDF;

    // register the vtable for this model number
    DTERR_C(dtinterval_espidf_register_vtables());

cleanup:
    if (dterr != NULL)
    {
        dtinterval_espidf_dispose(self);
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to initialize %s instance", CLASS_NAME);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtinterval_espidf_configure(dtinterval_espidf_t* self, dtinterval_espidf_config_t* config)
{
    self->config = *config;

    return NULL; // success
}

// --------------------------------------------------------------------------------------
dterr_t*
dtinterval_espidf_start(dtinterval_espidf_t* self)
{
    dterr_t* dterr = NULL;

    const esp_timer_create_args_t timer_args = { .callback = &dtinterval_espidf_isr, .name = self->config.name, .arg = self };
    DTMC_ESPIDF_C(esp_timer_create(&timer_args, &self->periodic_timer));
    DTMC_ESPIDF_C(esp_timer_start_periodic(self->periodic_timer, self->config.periodic_interval_micros));

    dtlog_debug(TAG,
      "%s(): started timer \"%s\" with period %" PRId32 " microseconds",
      __func__,
      self->config.name,
      self->config.periodic_interval_micros);

cleanup:

    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to start %s instance", CLASS_NAME);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtinterval_espidf_pause(dtinterval_espidf_t* self)
{
    dterr_t* dterr = NULL;

    if (self->periodic_timer != NULL)
    {
        DTMC_ESPIDF_C(esp_timer_stop(self->periodic_timer));
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to pause %s", CLASS_NAME);

    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtinterval_espidf_dispose(dtinterval_espidf_t* self)
{
    if (self == NULL)
        return;

    if (self->periodic_timer != NULL)
    {
        esp_timer_stop(self->periodic_timer);
        esp_timer_delete(self->periodic_timer);
    }

    if (self->_is_malloced)
    {
        free(self);
    }
    else
    {
        memset(self, 0, sizeof(*self));
    }
}
