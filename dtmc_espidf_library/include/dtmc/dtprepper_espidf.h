/*
 * dtprepper_espidf -- ESP-IDF startup sequencer for WiFi station and Ethernet.
 *
 * Orchestrates early-boot network initialization on ESP-IDF: loads radio
 * configuration, initializes the ESP system, and then brings up either a
 * WiFi station or an Ethernet interface (or both) in sequence. The struct
 * owns a dtradioconfig, a dtstation_espidf, and a dtethernet_espidf inline,
 * and exposes method pointers for each startup phase so the caller controls
 * the order of operations.
 *
 * cdox v1.0.2
 */
#ifndef DTPREPPER_H
#define DTPREPPER_H

#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtradioconfig.h>

#include <dtmc/dtethernet_espidf.h>
#include <dtmc/dtstation_espidf.h>

typedef struct dtprepper_espidf_t
{
    // Method members.
    dterr_t* (*configure)(struct dtprepper_espidf_t*);
    dterr_t* (*start_system)(struct dtprepper_espidf_t*);
    dterr_t* (*start_station)(struct dtprepper_espidf_t*);
    dterr_t* (*start_ethernet)(struct dtprepper_espidf_t*);
    void (*dispose)(struct dtprepper_espidf_t*);

    // Data members.
    dtradioconfig_t radioconfig;
    dtstation_espidf_config_t station_config;
    dtstation_espidf_t station;
    dtethernet_espidf_config_t ethernet_config;
    dtethernet_espidf_t ethernet;
} dtprepper_espidf_t;

dterr_t*
dtprepper_espidf_init(dtprepper_espidf_t* this);

#endif // DTPREPPER_H