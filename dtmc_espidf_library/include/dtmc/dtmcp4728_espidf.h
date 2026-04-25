/*
 * dtmcp4728_espidf -- ESP-IDF I2C driver for the MCP4728 quad 12-bit DAC.
 *
 * Drives a Microchip MCP4728 four-channel 12-bit DAC over ESP-IDF I2C.
 * Each channel is independently configured with a 12-bit output value,
 * voltage reference (VDD or internal 2.048 V), gain (x1 or x2), and power-
 * down mode. The API covers fast write (volatile only), multi write (one
 * channel at a time), sequential write (starting channel to N), and single
 * write with EEPROM commit, plus general-call reset, wake-up, and software
 * update. I2C port, GPIO pins, clock speed, and device address are all
 * configurable.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/i2c.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtmcp4728.h>

#define DTMCP4728_DEFAULT_I2C_PORT (I2C_NUM_0)
#define DTMCP4728_DEFAULT_SDA_PIN (GPIO_NUM_21)
#define DTMCP4728_DEFAULT_SCL_PIN (GPIO_NUM_22)
#define DTMCP4728_DEFAULT_I2C_CLOCK_HZ (100000)
#define DTMCP4728_DEFAULT_I2C_TIMEOUT_MS (1000)
#define DTMCP4728_DEFAULT_I2C_ADDRESS (0x60)

// --------------------------------------------------------------------------------------------
// Public object config

typedef struct dtmcp4728_espidf_config_t
{
    i2c_port_t i2c_port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t clock_speed_hz;
    uint32_t timeout_ms;
    uint8_t device_address_7bit;
    bool enable_pullups;
    bool install_driver;
} dtmcp4728_espidf_config_t;

// --------------------------------------------------------------------------------------------
// Concrete type

typedef struct dtmcp4728_espidf_t dtmcp4728_espidf_t;

// --------------------------------------------------------------------------------------------
// Lifecycle (not part of the facade)

void
dtmcp4728_espidf_config_init_defaults(dtmcp4728_espidf_config_t* cfg);

dterr_t*
dtmcp4728_espidf_create(dtmcp4728_espidf_t** self_ptr);

dterr_t*
dtmcp4728_espidf_init(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_configure(dtmcp4728_espidf_t* self, const dtmcp4728_espidf_config_t* config);

// --------------------------------------------------------------------------------------------
// Facade entry points

DTMCP4728_DECLARE_API(dtmcp4728_espidf)
