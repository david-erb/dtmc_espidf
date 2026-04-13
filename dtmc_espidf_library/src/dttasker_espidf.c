#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtguid.h>
#include <dtcore/dtguidable.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttasker_registry.h>

#include <dtmc/dtmc_espidf.h>

DTGUIDABLE_INIT_VTABLE(dttasker);

// --------------------------------------------------------------------------------------
#define TAG "dttasker"

#define DTTASKER_EVENT_READY_BIT BIT0
#define DTTASKER_EVENT_EXITED_BIT BIT1

// --------------------------------------------------------------------------------------
typedef struct dttasker_t
{
    int32_t model_number;
    char _name[32]; // storage for name

    dttasker_config_t config;

    EventGroupHandle_t event_group_handle;
    dttasker_info_t info;
    TaskHandle_t freertos_task_handle;

    // lifecycle / stop state
    atomic_bool stop_requested;
    atomic_bool task_exited;
    atomic_bool task_created;
    atomic_bool task_joined;

    dtguid_t guid; // 16 bytes, does not need to be aligned
} dttasker_t;

static dterr_t*
dttasker_priority_enum_to_native_number(dttasker_priority_t p, int32_t* native_number);

// --------------------------------------------------------------------------------------
extern dterr_t*
dttasker_create(dttasker_handle* self_handle, dttasker_config_t* config)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(self_handle);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->name);

    *self_handle = NULL;

    DTERR_C(dttasker_validate_priority_enum(config->priority));

    if (config->stack_size == 0)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "invalid config->stack_size=%" PRId32, config->stack_size);
        goto cleanup;
    }

    if (strlen(config->name) >= sizeof(((dttasker_t*)0)->_name))
    {
        dterr =
          dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "config->name too long (max %zu)", sizeof(((dttasker_t*)0)->_name) - 1);
        goto cleanup;
    }

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dttasker_t), "dttasker_t", (void**)&self));

    self->model_number = DTMC_BASE_CONSTANTS_TASKER_MODEL_ESPIDF;
    self->config = *config;

    // put the config name into internal storage
    strncpy(self->_name, config->name, sizeof(self->_name) - 1);
    self->_name[sizeof(self->_name) - 1] = '\0';

    DTERR_C(dtguidable_set_vtable(self->model_number, &dttasker_guidable_vt));
    dtguid_generate_from_string(&self->guid, self->_name);

    self->event_group_handle = xEventGroupCreate();
    if (self->event_group_handle == NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to create event group");
        goto cleanup;
    }

    self->freertos_task_handle = NULL;

    atomic_init(&self->stop_requested, false);
    atomic_init(&self->task_exited, false);
    atomic_init(&self->task_created, false);
    atomic_init(&self->task_joined, false);

    {
        dttasker_info_t info = {
            .status = INITIALIZED,
        };
        DTERR_C(dttasker_set_info((dttasker_handle)self, &info));
    }

    *self_handle = (dttasker_handle)self;

cleanup:
    if (dterr != NULL)
    {
        dttasker_dispose((dttasker_handle)self);
        *self_handle = NULL;
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_set_entry_point(dttasker_handle self_handle, dttasker_entry_point_fn entry_point_function, void* entry_point_arg)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    self->config.tasker_entry_point_fn = entry_point_function;
    self->config.tasker_entry_point_arg = entry_point_arg;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
static void
dttasker__thread_inception_function(void* pvParameters)
{
    dterr_t* dterr = NULL;
    dttasker_handle self_handle = (dttasker_handle)pvParameters;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(self->config.tasker_entry_point_fn(self->config.tasker_entry_point_arg, (dttasker_handle)self));

cleanup:
{
    dttasker_info_t info = { .status = STOPPED, .dterr = dterr };
    dttasker_set_info(self_handle, &info);
}

    // If start() is still waiting because the task failed before ready(), wake it.
    if (dterr != NULL)
    {
        xEventGroupSetBits(self->event_group_handle, DTTASKER_EVENT_READY_BIT);
    }

    // Mark permanent exited state for join().
    atomic_store_explicit(&self->task_exited, true, memory_order_release);
    self->freertos_task_handle = NULL;
    xEventGroupSetBits(self->event_group_handle, DTTASKER_EVENT_EXITED_BIT);

    vTaskDelete(NULL);
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_start(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    int32_t native_priority = 0;
    DTERR_C(dttasker_priority_enum_to_native_number(self->config.priority, &native_priority));

    // Refuse to start again if task is already running / not yet join-complete.
    if (atomic_load_explicit(&self->task_created, memory_order_acquire) &&
        !atomic_load_explicit(&self->task_exited, memory_order_acquire))
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "task \"%s\" is already started", self->_name);
        goto cleanup;
    }

    dtlog_debug(TAG,
      "%s(): called for task %s priority %s (native %" PRId32 ")",
      __func__,
      self->_name,
      dttasker_priority_enum_to_string(self->config.priority),
      native_priority);

    // Clear any stale event bits from a prior run.
    xEventGroupClearBits(self->event_group_handle, DTTASKER_EVENT_READY_BIT | DTTASKER_EVENT_EXITED_BIT);

    self->freertos_task_handle = NULL;

    atomic_store_explicit(&self->stop_requested, false, memory_order_release);
    atomic_store_explicit(&self->task_exited, false, memory_order_release);
    atomic_store_explicit(&self->task_joined, false, memory_order_release);
    atomic_store_explicit(&self->task_created, false, memory_order_release);

    BaseType_t rc = xTaskCreatePinnedToCore( //
      dttasker__thread_inception_function,   // task entry point function
      self->_name,                           // task name
      self->config.stack_size,               // stack depth in words, per FreeRTOS/ESP-IDF API
      self,                                  // user-provided context
      native_priority,                       // priority
      &self->freertos_task_handle,           // returned pointer to task
      self->config.core);                    // core affinity

    if (rc != pdPASS)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to create task \"%s\"", self->_name);
        goto cleanup;
    }

    atomic_store_explicit(&self->task_created, true, memory_order_release);

    // wait for the task to signal that it has started
    xEventGroupWaitBits(self->event_group_handle,
      DTTASKER_EVENT_READY_BIT,
      pdTRUE,  // clear ready event after consumption
      pdFALSE, // wait for any bit
      portMAX_DELAY);

cleanup:
    if (dterr != NULL)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "publisher task failed to start task \"%s\"", self->_name);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_ready(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    dttasker_info_t info = { .status = RUNNING };
    DTERR_C(dttasker_set_info(self_handle, &info));

    xEventGroupSetBits(self->event_group_handle, DTTASKER_EVENT_READY_BIT);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// request stop of the task (called from outside the task)
dterr_t*
dttasker_stop(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    atomic_store_explicit(&self->stop_requested, true, memory_order_release);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// wait for task to stop (called from outside the task)
dterr_t*
dttasker_join(dttasker_handle self_handle, dttimeout_millis_t timeout_millis, bool* was_timeout)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    TickType_t ticks = 0;
    EventBits_t bits = 0;

    DTERR_ASSERT_NOT_NULL(self);

    if (was_timeout != NULL)
    {
        *was_timeout = false;
    }

    // If never started or already joined, nothing to do.
    if (!atomic_load_explicit(&self->task_created, memory_order_acquire) ||
        atomic_load_explicit(&self->task_joined, memory_order_acquire))
    {
        goto cleanup;
    }

    // Fast path: task already exited.
    if (atomic_load_explicit(&self->task_exited, memory_order_acquire))
    {
        atomic_store_explicit(&self->task_joined, true, memory_order_release);
        atomic_store_explicit(&self->task_created, false, memory_order_release);
        goto cleanup;
    }

    if (timeout_millis == DTTIMEOUT_FOREVER)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_millis);

        // Avoid accidental zero-tick waits for small positive timeouts.
        if (timeout_millis > 0 && ticks == 0)
        {
            ticks = 1;
        }
    }

    bits = xEventGroupWaitBits(self->event_group_handle,
      DTTASKER_EVENT_EXITED_BIT, // wait for exit signal
      pdFALSE,                   // do NOT clear; exited state should remain sticky
      pdFALSE,                   // any bit
      ticks);

    if (!(bits & DTTASKER_EVENT_EXITED_BIT))
    {
        if (was_timeout != NULL)
        {
            *was_timeout = true;
        }
        else
        {
            dterr = dterr_new(DTERR_TIMEOUT,
              DTERR_LOC,
              NULL,
              "task \"%s\" did not join within timeout %" DTTIMEOUT_MILLIS_PRI " milliseconds",
              self->_name,
              timeout_millis);
        }
        goto cleanup;
    }

    atomic_store_explicit(&self->task_joined, true, memory_order_release);
    atomic_store_explicit(&self->task_created, false, memory_order_release);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// poll if task is supposed to stop (called from client code inside the task)
dterr_t*
dttasker_poll(dttasker_handle self_handle, bool* should_stop)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(should_stop);

    *should_stop = atomic_load_explicit(&self->stop_requested, memory_order_acquire);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_set_priority(dttasker_handle self_handle, dttasker_priority_t priority)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;

    int32_t native_priority = 0;

    DTERR_C(dttasker_validate_priority_enum(priority));
    DTERR_C(dttasker_priority_enum_to_native_number(priority, &native_priority));

    if (self == NULL)
    {
        // FreeRTOS convention: NULL means "current task"
        dtlog_debug(TAG,
          "setting current task priority to %s (native %" PRId32 ")",
          dttasker_priority_enum_to_string(priority),
          native_priority);

        vTaskPrioritySet(NULL, (UBaseType_t)native_priority);
        goto cleanup;
    }

    if (!atomic_load_explicit(&self->task_created, memory_order_acquire) || self->freertos_task_handle == NULL)
    {
        dterr = dterr_new(
          DTERR_FAIL, DTERR_LOC, NULL, "cannot set priority for task \"%s\" because it has not been started", self->_name);
        goto cleanup;
    }

    dtlog_debug(TAG,
      "setting task %s priority to %s (native %" PRId32 ")",
      self->_name,
      dttasker_priority_enum_to_string(priority),
      native_priority);

    vTaskPrioritySet(self->freertos_task_handle, (UBaseType_t)native_priority);

    // Keep the configured priority in sync with what is now applied.
    self->config.priority = priority;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_get_info(dttasker_handle self_handle, dttasker_info_t* info)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(info);

    *info = self->info;

    // always fill the info with the task's originally configured name
    info->name = info->_name;
    strncpy(info->name, self->_name, sizeof(info->_name));
    info->name[sizeof(info->_name) - 1] = '\0';

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_set_info(dttasker_handle self_handle, dttasker_info_t* info)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(info);

    self->info = *info;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_get_guid(dttasker_handle self_handle, dtguid_t* guid)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(guid);

    dtguid_copy(guid, &self->guid);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
void
dttasker_dispose(dttasker_handle self_handle)
{
    dttasker_t* self = (dttasker_t*)self_handle;
    if (self == NULL)
        return;

    if (self->event_group_handle != NULL)
        vEventGroupDelete(self->event_group_handle);

    dtheaper_free(self);
}

// =============================================================================
// FREERTOS (ESP-IDF): fixed mapping (no scaling)
// =============================================================================
//
// Requirements:
//   - Always preemptive priorities only (ESP-IDF FreeRTOS is preemptive by config)
//   - At least 15 priorities available
//   - Native FreeRTOS task priorities: [0 .. 14]
//     (higher number => higher urgency)
//
// Mapping policy (15 levels total):
//   BACKGROUND_* -> 0 .. 4
//   NORMAL_*     -> 5 .. 9
//   URGENT_*     -> 10 .. 14
//

#if (configMAX_PRIORITIES < 15)
#error "This project requires configMAX_PRIORITIES >= 15 (fixed priority mapping)."
#endif

static dterr_t*
dttasker_priority_enum_to_native_number(dttasker_priority_t p, int32_t* native_priority)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(native_priority);

    // In FreeRTOS, numerically higher priorities take precedence over lower values.
    switch (p)
    {
        // Stable default: middle of NORMAL band.
        case DTTASKER_PRIORITY_DEFAULT_FOR_SITUATION:
            *native_priority = 7; // NORMAL_MEDIUM
            return NULL;

        // BACKGROUND band (lowest urgency)
        case DTTASKER_PRIORITY_BACKGROUND_LOWEST:
            *native_priority = 0;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_LOW:
            *native_priority = 1;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_MEDIUM:
            *native_priority = 2;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_HIGH:
            *native_priority = 3;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_HIGHEST:
            *native_priority = 4;
            return NULL;

        // NORMAL band
        case DTTASKER_PRIORITY_NORMAL_LOWEST:
            *native_priority = 5;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_LOW:
            *native_priority = 6;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_MEDIUM:
            *native_priority = 7;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_HIGH:
            *native_priority = 8;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_HIGHEST:
            *native_priority = 9;
            return NULL;

        // URGENT band (highest urgency)
        case DTTASKER_PRIORITY_URGENT_LOWEST:
            *native_priority = 10;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_LOW:
            *native_priority = 11;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_MEDIUM:
            *native_priority = 12;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_HIGH:
            *native_priority = 13;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_HIGHEST:
            *native_priority = 14;
            return NULL;

        // markers / invalid
        case DTTASKER_PRIORITY__START:
        case DTTASKER_PRIORITY__COUNT:
        default:
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "unknown dttasker_priority_t value %" PRId32, (int32_t)p);
            goto cleanup;
    }

cleanup:
    return dterr;
}