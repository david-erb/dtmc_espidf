/*
 * dtnetportal_espmqtt -- ESP-IDF MQTT backend for the dtnetportal interface.
 *
 * Implements the dtnetportal vtable using the ESP-IDF MQTT client stack.
 * Broker host, port, credentials, and WiFi parameters are supplied through
 * a dtradioconfig reference, keeping provisioning data in one place. A
 * configurable retry limit governs reconnect attempts before the backend
 * reports a permanent failure through the standard dterr_t error chain.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtradioconfig.h>

#define DTNETPORTAL_ESPMQTT_FLAVOR "dtnetportal_espmqtt"
#define DTNETPORTAL_ESPMQTT_VERSION "0.0.1"

typedef struct dtnetportal_espmqtt_config_t
{
    dtradioconfig_t* radioconfig;
    int retry_count_maximum;
} dtnetportal_espmqtt_config_t;

typedef struct dtnetportal_espmqtt_t dtnetportal_espmqtt_t;

extern dterr_t*
dtnetportal_espmqtt_create(dtnetportal_espmqtt_t** self_ptr);

extern dterr_t*
dtnetportal_espmqtt_init(dtnetportal_espmqtt_t* self);

extern dterr_t*
dtnetportal_espmqtt_configure(dtnetportal_espmqtt_t* self, dtnetportal_espmqtt_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTNETPORTAL_DECLARE_API(dtnetportal_espmqtt);
DTOBJECT_DECLARE_API(dtnetportal_espmqtt);
