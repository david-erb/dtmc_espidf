/*
 * dtadc_espidf_oneshot -- ESP-IDF one-shot ADC backend for the dtadc interface.
 *
 * Implements the dtadc vtable using the ESP-IDF one-shot ADC driver. Up to
 * four channels are configured by GPIO number, attenuation, and bit width;
 * the backend resolves GPIO numbers to ADC unit and channel internally so
 * the public config remains free of ESP-IDF-specific types. A background
 * task scans all configured channels at a configurable interval and delivers
 * results via a per-scan callback.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtadc.h>

// Forward-declare concrete type
typedef struct dtadc_espidf_oneshot_t dtadc_espidf_oneshot_t;

#define DTADC_ESPIDF_ONESHOT_MAX_CHANNELS (4)
#define DTADC_ESPIDF_ONESHOT_DEFAULT_SCAN_INTERVAL_MS 100
#define DTADC_ESPIDF_ONESHOT_DEFAULT_TASK_CORE 0

// Use GPIO in the public config because that is usually what the caller knows.
// The backend resolves GPIO -> adc_unit_t + adc_channel_t internally.
typedef struct
{
    int32_t gpio_num;

    // ESP-IDF ADC attenuation enum value, stored as int32_t in the public header
    // so callers do not need ESP-IDF headers here.
    // Typical value:
    //   ADC_ATTEN_DB_12
    int32_t attenuation;

    // ESP-IDF bitwidth enum value, stored as int32_t in the public header.
    // Typical values:
    //   ADC_BITWIDTH_DEFAULT
    //   ADC_BITWIDTH_12
    int32_t bitwidth;
} dtadc_espidf_oneshot_channel_config_t;

typedef struct
{
    // Time between scan batches.
    // One "batch" means: read each configured channel once and deliver one callback.
    int32_t scan_interval_ms;

    // Which core the background reader task should run on.
    // For a first implementation, 0 is fine.
    int32_t task_core;

    // Optional task tuning. Use 0 to get backend defaults.
    int32_t task_stack_size;
    int32_t task_priority;

    int32_t nchannels;
    dtadc_espidf_oneshot_channel_config_t channels[DTADC_ESPIDF_ONESHOT_MAX_CHANNELS];

} dtadc_espidf_oneshot_config_t;

// Optional helper to populate sane defaults before the caller edits fields.
extern void
dtadc_espidf_oneshot_config_init_defaults(dtadc_espidf_oneshot_config_t* cfg);

// Lifecycle / configuration
extern dterr_t*
dtadc_espidf_oneshot_create(dtadc_espidf_oneshot_t** self_ptr);

extern dterr_t*
dtadc_espidf_oneshot_init(dtadc_espidf_oneshot_t* self);

extern dterr_t*
dtadc_espidf_oneshot_configure(dtadc_espidf_oneshot_t* self, const dtadc_espidf_oneshot_config_t* cfg);

// Interface plumbing (vtable targets)
DTADC_DECLARE_API(dtadc_espidf_oneshot);