/*
 * dtgpiopin_espidf -- ESP-IDF GPIO backend for the dtgpiopin cross-platform interface.
 *
 * Implements the dtgpiopin vtable for ESP32-class SoCs using the ESP-IDF
 * GPIO driver. A single GPIO number (0-40 typical) is combined with mode
 * (input/output/inout), pull (none/up/down), and drive strength at
 * configuration time. ISR attach and enable/disable follow the common
 * dtgpiopin contract, leaving application code portable across platforms.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdint.h>

#include <dtmc_base/dtgpiopin.h>

typedef struct dtgpiopin_espidf_t dtgpiopin_espidf_t;

typedef struct
{
    uint8_t pin_number;      // GPIO number (e.g. 0..39 on ESP32-class parts)
    dtgpiopin_mode_t mode;   // input / output / inout
    dtgpiopin_pull_t pull;   // none / up / down
    dtgpiopin_drive_t drive; // default / open-drain / weak / strong
} dtgpiopin_espidf_config_t;

dterr_t*
dtgpiopin_espidf_create(dtgpiopin_espidf_t** self_ptr);
dterr_t*
dtgpiopin_espidf_init(dtgpiopin_espidf_t* self);
dterr_t*
dtgpiopin_espidf_configure(dtgpiopin_espidf_t* self, const dtgpiopin_espidf_config_t* config);

DTGPIOPIN_DECLARE_API_EX(dtgpiopin_espidf, _t*);

#ifndef DTGPIOPIN_ESPIDF_MAX_PIN
// Conservative default; actual valid range depends on the specific SoC.
#define DTGPIOPIN_ESPIDF_MAX_PIN 40
#endif
