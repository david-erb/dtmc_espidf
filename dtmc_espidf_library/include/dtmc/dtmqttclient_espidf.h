/*
 * dtmqttclient_espidf -- ESP-IDF MQTT client backend for the dtmqttclient interface.
 *
 * Implements the dtmqttclient vtable using the ESP-IDF MQTT client stack.
 * Connection parameters are drawn from a dtradioconfig reference, avoiding
 * redundant broker and credential fields at each call site. A configurable
 * retry limit controls reconnect attempts before a persistent failure is
 * reported.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>

#include <dtcore/dtmqttclient.h>
#include <dtmc_base/dtradioconfig.h>

typedef struct dtmqttclient_esp_config_t
{
    dtradioconfig_t* radioconfig;
    int retry_count_maximum;
} dtmqttclient_esp_config_t;

typedef struct dtmqttclient_esp_t dtmqttclient_esp_t;

extern dterr_t*
dtmqttclient_esp_create(dtmqttclient_esp_t** self_ptr);

extern dterr_t*
dtmqttclient_esp_init(dtmqttclient_esp_t* self);

extern dterr_t*
dtmqttclient_esp_configure(dtmqttclient_esp_t* self, dtmqttclient_esp_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTMQTTCLIENT_DECLARE_API(dtmqttclient_esp);
