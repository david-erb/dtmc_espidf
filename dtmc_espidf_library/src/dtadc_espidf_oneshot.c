// ESP-IDF backend for dtadc (CPU sampling, no DMA).
// This simple implementation uses adc_oneshot and reads each configured channel
// individually from a background task.

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtadc.h>
#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtadc_espidf_oneshot.h>

#include <dtmc/dtmc.h>

#define TAG "dtadc_espidf_oneshot"
// #define dtlog_debug(TAG, ...)

// vtable
DTADC_INIT_VTABLE(dtadc_espidf_oneshot);

// Tunable defaults
#define DTADC_ESPIDF_ONESHOT_READER_TASK_STACK (4096)
#define DTADC_ESPIDF_ONESHOT_READER_TASK_PRIORITY (DTTASKER_PRIORITY_URGENT_MEDIUM)

// Internal: max ESP-IDF ADC units supported by this backend.
#define DTADC_ESPIDF_ONESHOT_MAX_UNITS (2)

typedef struct
{
    int32_t gpio_num;
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t attenuation;
    adc_bitwidth_t bitwidth;
    int32_t unit_index; // index into unit_handles[]
} dtadc_espidf_oneshot_channel_runtime_t;

// -----------------------------------------------------------------------------
// Concrete type

typedef struct dtadc_espidf_oneshot_t
{
    DTADC_COMMON_MEMBERS

    dtadc_espidf_oneshot_config_t config;

    bool is_configured;
    bool is_active;

    // protects status and activation state
    dtlock_handle lock;

    // signal to background reader task to stop when deactivated
    dtsemaphore_handle reader_tasker_should_stop_semaphore;

    // high priority background task continuously reads scans from the hardware
    dttasker_handle reader_tasker_handle;

    dtadc_scan_callback_fn scan_callback_fn;
    void* scan_callback_context;

    // pre-allocated scan struct to reuse for callbacks to avoid heap alloc in hot path
    dtadc_scan_t scan;

    // raw values, one per configured channel
    int32_t channels[DTADC_ESPIDF_ONESHOT_MAX_CHANNELS];

    // resolved runtime channel descriptors
    dtadc_espidf_oneshot_channel_runtime_t channel_runtime[DTADC_ESPIDF_ONESHOT_MAX_CHANNELS];

    // created lazily during activate()
    adc_oneshot_unit_handle_t unit_handles[DTADC_ESPIDF_ONESHOT_MAX_UNITS];
    adc_unit_t units[DTADC_ESPIDF_ONESHOT_MAX_UNITS];
    int32_t nunit_handles;

    dtadc_status_t status;
} dtadc_espidf_oneshot_t;

static dterr_t*
dtadc_espidf_oneshot__reader_task_entry(void* arg, dttasker_handle tasker_handle);

static dterr_t*
dtadc_espidf_oneshot__validate_config(const dtadc_espidf_oneshot_config_t* cfg);

static dterr_t*
dtadc_espidf_oneshot__open_hardware(dtadc_espidf_oneshot_t* self);

static void
dtadc_espidf_oneshot__close_hardware(dtadc_espidf_oneshot_t* self);

static dterr_t*
dtadc_espidf_oneshot__resolve_channel(const dtadc_espidf_oneshot_channel_config_t* src,
  dtadc_espidf_oneshot_channel_runtime_t* dst);

static int32_t
dtadc_espidf_oneshot__find_or_add_unit_handle(dtadc_espidf_oneshot_t* self, adc_unit_t unit);

// -----------------------------------------------------------------------------
// Defaults

void
dtadc_espidf_oneshot_config_init_defaults(dtadc_espidf_oneshot_config_t* cfg)
{
    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->scan_interval_ms = DTADC_ESPIDF_ONESHOT_DEFAULT_SCAN_INTERVAL_MS;
    cfg->task_core = DTADC_ESPIDF_ONESHOT_DEFAULT_TASK_CORE;
    cfg->task_stack_size = DTADC_ESPIDF_ONESHOT_READER_TASK_STACK;
    cfg->task_priority = DTADC_ESPIDF_ONESHOT_READER_TASK_PRIORITY;

    // Pick 4 safe ADC1 pins
    cfg->nchannels = 4;

    cfg->channels[0].gpio_num = 32;
    cfg->channels[1].gpio_num = 33;
    cfg->channels[2].gpio_num = 34;
    cfg->channels[3].gpio_num = 35;

    // Reasonable defaults for ESP32 ADC
    for (int i = 0; i < cfg->nchannels; i++)
    {
        cfg->channels[i].attenuation = ADC_ATTEN_DB_12; // full range ~0–3.3V
        cfg->channels[i].bitwidth = ADC_BITWIDTH_DEFAULT;
    }
}

// -----------------------------------------------------------------------------
// Lifecycle

dterr_t*
dtadc_espidf_oneshot_create(dtadc_espidf_oneshot_t** self_ptr)
{
    dterr_t* dterr = NULL;
    dtadc_espidf_oneshot_t* self = NULL;

    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtadc_espidf_oneshot_t), "dtadc_espidf_oneshot_t", (void**)&self));

    *self_ptr = self;

    DTERR_C(dtadc_espidf_oneshot_init(self));

cleanup:
    if (dterr)
    {
        dtheaper_free(self);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_espidf_oneshot_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_espidf_oneshot_init(dtadc_espidf_oneshot_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_ADC_MODEL_ESPIDF_ONESHOT;
    self->scan.channels = self->channels;

    DTERR_C(dtadc_set_vtable(self->model_number, &dtadc_espidf_oneshot_vt));
    DTERR_C(dtlock_create(&self->lock));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_espidf_oneshot_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_espidf_oneshot_configure(dtadc_espidf_oneshot_t* self, const dtadc_espidf_oneshot_config_t* config)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    if (self->is_active)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot configure while active");
        goto cleanup;
    }

    DTERR_C(dtadc_espidf_oneshot__validate_config(config));

    self->config = *config;
    self->is_configured = true;

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_espidf_oneshot_configure failed");
    return dterr;
}

// -----------------------------------------------------------------------------
// Vtable targets

dterr_t*
dtadc_espidf_oneshot_activate(dtadc_espidf_oneshot_t* self DTADC_ACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;
    dttasker_config_t tasker_config = { 0 };
    bool is_locked = false;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(scan_callback_fn);

    if (!self->is_configured)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtadc_espidf_oneshot must be configured before activate");
        goto cleanup;
    }

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    if (self->is_active)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtadc_espidf_oneshot already active");
        goto cleanup;
    }

    self->scan_callback_fn = scan_callback_fn;
    self->scan_callback_context = scan_callback_context;
    self->status.dterr = NULL;
    self->status.state = DTADC_STATE_STARTING;

    DTERR_C(dtadc_espidf_oneshot__open_hardware(self));
    DTERR_C(dtsemaphore_create(&self->reader_tasker_should_stop_semaphore, 0, 0));

    tasker_config.name = "dtadc";
    tasker_config.tasker_entry_point_fn = dtadc_espidf_oneshot__reader_task_entry;
    tasker_config.tasker_entry_point_arg = self;
    tasker_config.stack_size =
      self->config.task_stack_size > 0 ? self->config.task_stack_size : DTADC_ESPIDF_ONESHOT_READER_TASK_STACK;
    tasker_config.priority =
      self->config.task_priority > 0 ? self->config.task_priority : DTADC_ESPIDF_ONESHOT_READER_TASK_PRIORITY;
    tasker_config.core = self->config.task_core;

    DTERR_C(dttasker_create(&self->reader_tasker_handle, &tasker_config));
    DTERR_C(dttasker_start(self->reader_tasker_handle));

    self->is_active = true;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);

    if (dterr)
    {
        dtadc_espidf_oneshot__close_hardware(self);
        dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
        self->reader_tasker_should_stop_semaphore = NULL;
        dttasker_dispose(self->reader_tasker_handle);
        self->reader_tasker_handle = NULL;
        self->is_active = false;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_espidf_oneshot_activate failed");
    }

    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_espidf_oneshot_deactivate(dtadc_espidf_oneshot_t* self DTADC_DEACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    if (!self->is_active)
        goto cleanup;

    if (self->reader_tasker_should_stop_semaphore)
        DTERR_C(dtsemaphore_post(self->reader_tasker_should_stop_semaphore));

    if (is_locked)
    {
        dtlock_release(self->lock);
        is_locked = false;
    }

    // crude but acceptable for a first backend
    for (i = 0; i < 50; i++)
    {
        dtruntime_sleep_milliseconds(10);

        DTERR_C(dtlock_acquire(self->lock));
        is_locked = true;

        if (self->status.state == DTADC_STATE_STOPPED || self->status.state == DTADC_STATE_ERROR)
            break;

        dtlock_release(self->lock);
        is_locked = false;
    }

    dttasker_dispose(self->reader_tasker_handle);
    self->reader_tasker_handle = NULL;

    dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
    self->reader_tasker_should_stop_semaphore = NULL;

    dtadc_espidf_oneshot__close_hardware(self);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;
    self->is_active = false;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);

    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_espidf_oneshot_deactivate failed");
    return dterr;
}

// -----------------------------------------------------------------------------
//
// In practical use, this backend is expected to be started once and run until
// reboot. dispose() is mainly for tests or error paths.

void
dtadc_espidf_oneshot_dispose(dtadc_espidf_oneshot_t* self)
{
    if (!self)
        return;

    if (self->is_active)
        dtadc_espidf_oneshot_deactivate(self);

    dtadc_espidf_oneshot__close_hardware(self);

    dttasker_dispose(self->reader_tasker_handle);
    self->reader_tasker_handle = NULL;

    dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
    self->reader_tasker_should_stop_semaphore = NULL;

    dtlock_dispose(self->lock);
    self->lock = NULL;

    memset(self, 0, sizeof(*self));
    dtheaper_free(self);
}

// --------------------------------------------------------------------------------------------
// return status

dterr_t*
dtadc_espidf_oneshot_get_status(dtadc_espidf_oneshot_t* self DTADC_GET_STATUS_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(status);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    *status = self->status;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);
    return dterr;
}

// --------------------------------------------------------------------------------------------
// Convert to string
dterr_t*
dtadc_espidf_oneshot_to_string(dtadc_espidf_oneshot_t* self, char* buffer, int32_t buffer_size)
{
    dterr_t* dterr = NULL;
    int32_t i;
    int32_t offset = 0;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    if (buffer_size <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "buffer_size must be > 0");
        goto cleanup;
    }

    // Base
    offset += snprintf(buffer + offset, buffer_size - offset, "dtadc_espidf_oneshot");

    // Config summary
    offset += snprintf(buffer + offset,
      buffer_size - offset,
      " nch=%" PRId32 " interval=%" PRId32 "ms",
      self->config.nchannels,
      self->config.scan_interval_ms);

    // Channels
    offset += snprintf(buffer + offset, buffer_size - offset, " [");

    for (i = 0; i < self->config.nchannels; i++)
    {
        int32_t gpio = self->config.channels[i].gpio_num;

        if (i > 0)
            offset += snprintf(buffer + offset, buffer_size - offset, ", ");

        // If runtime info is available (after activate/open_hardware)
        if (self->channel_runtime[i].unit_index >= 0)
        {
            offset += snprintf(buffer + offset,
              buffer_size - offset,
              "GPIO%" PRId32 "->U%dC%d",
              gpio,
              (int)self->channel_runtime[i].unit,
              (int)self->channel_runtime[i].channel);
        }
        else
        {
            // Try best-effort resolution even before activation
            adc_unit_t unit;
            adc_channel_t channel;
            esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &channel);

            if (err == ESP_OK)
            {
                offset +=
                  snprintf(buffer + offset, buffer_size - offset, "GPIO%" PRId32 "->U%dC%d?", gpio, (int)unit, (int)channel);
            }
            else
            {
                offset += snprintf(buffer + offset, buffer_size - offset, "GPIO%" PRId32 "->?", gpio);
            }
        }

        // Stop if buffer is full
        if (offset >= buffer_size)
            break;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "]");

    // Ensure null termination
    buffer[buffer_size - 1] = '\0';

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Reader task

static dterr_t*
dtadc_espidf_oneshot__reader_task_entry(void* arg, dttasker_handle tasker_handle)
{
    dterr_t* dterr = NULL;
    dtadc_espidf_oneshot_t* self = (dtadc_espidf_oneshot_t*)arg;
    bool is_locked = false;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    self->status.state = DTADC_STATE_ACTIVE;

    DTERR_C(dttasker_ready(tasker_handle));

    for (;;)
    {
        bool was_timeout = false;

        DTERR_C(dtsemaphore_wait(self->reader_tasker_should_stop_semaphore, self->config.scan_interval_ms, &was_timeout));
        if (!was_timeout)
            break;

        for (i = 0; i < self->config.nchannels; i++)
        {
            adc_oneshot_unit_handle_t unit_handle;
            int raw = 0;
            int32_t unit_index = self->channel_runtime[i].unit_index;
            esp_err_t err;

            if (unit_index < 0 || unit_index >= self->nunit_handles)
            {
                dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "invalid adc unit index");
                goto cleanup;
            }

            unit_handle = self->unit_handles[unit_index];
            err = adc_oneshot_read(unit_handle, self->channel_runtime[i].channel, &raw);
            if (err != ESP_OK)
            {
                dterr = dterr_new(DTERR_STATE,
                  DTERR_LOC,
                  NULL,
                  "adc_oneshot_read failed for gpio %d, err=%d",
                  self->channel_runtime[i].gpio_num,
                  (int)err);
                goto cleanup;
            }

            self->channels[i] = raw;
        }

        self->scan.channels = self->channels;
        self->scan.timestamp_ns = dtruntime_now_milliseconds() * 1000000ULL;

        DTERR_C(self->scan_callback_fn(self->scan_callback_context, &self->scan));
    }

cleanup:
    if (is_locked)
        dtlock_release(self->lock);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    if (dterr)
    {
        dtlog_error(TAG, "reader task exiting with error: %s", dterr->message);
        // keep only first error if multiple, to avoid overwriting with subsequent errors during shutdown
        if (self->status.dterr == NULL)
            self->status.dterr = dterr_new(dterr->error_code, DTERR_LOC, NULL, "reader task error");
        self->status.state = DTADC_STATE_ERROR;
    }
    else
    {
        dtlog_debug(TAG, "reader task exiting without error");
        self->status.state = DTADC_STATE_STOPPED;
    }

    if (is_locked)
        dtlock_release(self->lock);

    return dterr;
}

// ------------------------------------------------------------------
// Helpers

static dterr_t*
dtadc_espidf_oneshot__validate_config(const dtadc_espidf_oneshot_config_t* cfg)
{
    dterr_t* dterr = NULL;
    int32_t i;
    int32_t j;

    DTERR_ASSERT_NOT_NULL(cfg);

    if (cfg->scan_interval_ms <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "scan_interval_ms must be > 0");
        goto cleanup;
    }

    if (cfg->nchannels <= 0 || cfg->nchannels > DTADC_ESPIDF_ONESHOT_MAX_CHANNELS)
    {
        dterr =
          dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "nchannels must be between 1 and %d", DTADC_ESPIDF_ONESHOT_MAX_CHANNELS);
        goto cleanup;
    }

    for (i = 0; i < cfg->nchannels; i++)
    {
        adc_unit_t unit;
        adc_channel_t channel;
        esp_err_t err;

        if (cfg->channels[i].gpio_num < 0)
        {
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "channels[%d].gpio_num must be >= 0", i);
            goto cleanup;
        }

        err = adc_oneshot_io_to_channel(cfg->channels[i].gpio_num, &unit, &channel);
        if (err != ESP_OK)
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "channels[%d].gpio_num %d is not a valid ADC pin for this ESP target",
              i,
              cfg->channels[i].gpio_num);
            goto cleanup;
        }

        if (unit != ADC_UNIT_1)
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "channels[%d].gpio_num %d maps to ADC2, which this backend does not allow because ADC2 can conflict with Wi-Fi; "
              "use ADC1 pins instead (for classic ESP32, good choices are GPIO32, GPIO33, GPIO34, GPIO35, GPIO36, GPIO39)",
              i,
              cfg->channels[i].gpio_num);
            goto cleanup;
        }

        for (j = i + 1; j < cfg->nchannels; j++)
        {
            if (cfg->channels[i].gpio_num == cfg->channels[j].gpio_num)
            {
                dterr =
                  dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "duplicate gpio_num %d in channel list", cfg->channels[i].gpio_num);
                goto cleanup;
            }
        }
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

static dterr_t*
dtadc_espidf_oneshot__resolve_channel(const dtadc_espidf_oneshot_channel_config_t* src,
  dtadc_espidf_oneshot_channel_runtime_t* dst)
{
    dterr_t* dterr = NULL;
    esp_err_t err;
    adc_unit_t unit;
    adc_channel_t channel;

    DTERR_ASSERT_NOT_NULL(src);
    DTERR_ASSERT_NOT_NULL(dst);

    err = adc_oneshot_io_to_channel(src->gpio_num, &unit, &channel);
    if (err != ESP_OK)
    {
        dterr = dterr_new(DTERR_BADARG,
          DTERR_LOC,
          NULL,
          "gpio %d is not a valid ADC pin for adc_oneshot_io_to_channel (err=%d)",
          src->gpio_num,
          (int)err);
        goto cleanup;
    }

    memset(dst, 0, sizeof(*dst));
    dst->gpio_num = src->gpio_num;
    dst->unit = unit;
    dst->channel = channel;
    dst->attenuation = (adc_atten_t)src->attenuation;
    dst->bitwidth = (adc_bitwidth_t)src->bitwidth;
    dst->unit_index = -1;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

static int32_t
dtadc_espidf_oneshot__find_or_add_unit_handle(dtadc_espidf_oneshot_t* self, adc_unit_t unit)
{
    int32_t i;
    esp_err_t err;
    adc_oneshot_unit_init_cfg_t init_cfg = { 0 };
    adc_oneshot_unit_handle_t handle = NULL;

    for (i = 0; i < self->nunit_handles; i++)
    {
        if (self->units[i] == unit)
            return i;
    }

    if (self->nunit_handles >= DTADC_ESPIDF_ONESHOT_MAX_UNITS)
        return -1;

    init_cfg.unit_id = unit;
    init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    err = adc_oneshot_new_unit(&init_cfg, &handle);
    if (err != ESP_OK)
        return -1;

    i = self->nunit_handles++;
    self->units[i] = unit;
    self->unit_handles[i] = handle;
    return i;
}

// -----------------------------------------------------------------------------

static dterr_t*
dtadc_espidf_oneshot__open_hardware(dtadc_espidf_oneshot_t* self)
{
    dterr_t* dterr = NULL;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    self->nunit_handles = 0;
    memset(self->unit_handles, 0, sizeof(self->unit_handles));
    memset(self->units, 0, sizeof(self->units));
    memset(self->channel_runtime, 0, sizeof(self->channel_runtime));

    for (i = 0; i < self->config.nchannels; i++)
    {
        adc_oneshot_chan_cfg_t chan_cfg = { 0 };
        int32_t unit_index;
        esp_err_t err;

        DTERR_C(dtadc_espidf_oneshot__resolve_channel(&self->config.channels[i], &self->channel_runtime[i]));

        unit_index = dtadc_espidf_oneshot__find_or_add_unit_handle(self, self->channel_runtime[i].unit);
        if (unit_index < 0)
        {
            dterr = dterr_new(DTERR_STATE,
              DTERR_LOC,
              NULL,
              "failed to create/find ADC unit handle for gpio %d",
              self->config.channels[i].gpio_num);
            goto cleanup;
        }

        self->channel_runtime[i].unit_index = unit_index;

        chan_cfg.atten = self->channel_runtime[i].attenuation;
        chan_cfg.bitwidth = self->channel_runtime[i].bitwidth;

        err = adc_oneshot_config_channel(self->unit_handles[unit_index], self->channel_runtime[i].channel, &chan_cfg);
        if (err != ESP_OK)
        {
            dterr = dterr_new(DTERR_STATE,
              DTERR_LOC,
              NULL,
              "adc_oneshot_config_channel failed for gpio %d (err=%d)",
              self->config.channels[i].gpio_num,
              (int)err);
            goto cleanup;
        }
    }

cleanup:
    if (dterr)
        dtadc_espidf_oneshot__close_hardware(self);

    return dterr;
}

// -----------------------------------------------------------------------------

static void
dtadc_espidf_oneshot__close_hardware(dtadc_espidf_oneshot_t* self)
{
    int32_t i;

    if (!self)
        return;

    for (i = 0; i < self->nunit_handles; i++)
    {
        if (self->unit_handles[i])
        {
            adc_oneshot_del_unit(self->unit_handles[i]);
            self->unit_handles[i] = NULL;
        }
    }

    self->nunit_handles = 0;
}