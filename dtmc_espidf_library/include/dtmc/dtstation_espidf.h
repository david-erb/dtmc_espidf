/*
 * dtstation_espidf -- ESP-IDF WiFi station connection manager.
 *
 * Manages the ESP-IDF WiFi stack in station mode: configure from a
 * dtradioconfig reference, attempt connection with a bounded retry count,
 * and query link status. Connection state is tracked internally via a
 * FreeRTOS event group. The struct exposes method pointers directly,
 * providing a lightweight object interface without the model-number
 * registry used by later modules.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtradioconfig.h>

typedef struct dtstation_espidf_config_t
{
    dtradioconfig_t* radioconfig;
    int retry_count_maximum;
} dtstation_espidf_config_t;

typedef struct dtstation_espidf_t
{
    // data members
    dtstation_espidf_config_t* config;
    EventGroupHandle_t event_group_handle;
    int retry_count;
    bool connection_failed;

    // method members
    dterr_t* (*configure)(struct dtstation_espidf_t*, dtstation_espidf_config_t*);
    dterr_t* (*connect)(struct dtstation_espidf_t*);
    bool (*is_connected)(struct dtstation_espidf_t*);
    void (*dispose)(struct dtstation_espidf_t*);
} dtstation_espidf_t;

dterr_t*
dtstation_init(dtstation_espidf_t*);
