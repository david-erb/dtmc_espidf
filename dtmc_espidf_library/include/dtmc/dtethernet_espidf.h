/*
 * dtethernet_espidf -- ESP-IDF Ethernet connection manager.
 *
 * Manages the ESP-IDF Ethernet driver lifecycle: configure from a
 * dtradioconfig reference, connect, and query link status. Connection
 * state is tracked internally via a FreeRTOS event group. The struct
 * exposes method pointers directly, providing a lightweight object
 * interface without the model-number registry used by later modules.
 *
 * cdox v1.0.2
 */
#ifndef DTETHERNET_H
#define DTETHERNET_H

#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtradioconfig.h>

typedef struct dtethernet_espidf_config_t
{
    dtradioconfig_t* radioconfig;
} dtethernet_espidf_config_t;

typedef struct dtethernet_espidf_t
{
    // method members
    dterr_t* (*configure)(struct dtethernet_espidf_t*, dtethernet_espidf_config_t*);
    dterr_t* (*connect)(struct dtethernet_espidf_t*);
    bool (*is_connected)(struct dtethernet_espidf_t*);
    void (*dispose)(struct dtethernet_espidf_t*);

    // data members
    dtethernet_espidf_config_t* config;
    EventGroupHandle_t event_group_handle;
    bool connection_succeeded;
} dtethernet_espidf_t;

dterr_t*
dtethernet_init(dtethernet_espidf_t*);

#endif // DTETHERNET_H