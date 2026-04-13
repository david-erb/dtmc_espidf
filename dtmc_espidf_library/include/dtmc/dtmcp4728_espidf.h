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

#define DTMCP4728_DEFAULT_I2C_PORT (I2C_NUM_0)
#define DTMCP4728_DEFAULT_SDA_PIN (GPIO_NUM_21)
#define DTMCP4728_DEFAULT_SCL_PIN (GPIO_NUM_22)
#define DTMCP4728_DEFAULT_I2C_CLOCK_HZ (100000)
#define DTMCP4728_DEFAULT_I2C_TIMEOUT_MS (1000)
#define DTMCP4728_DEFAULT_I2C_ADDRESS (0x60)

#define DTMCP4728_CHANNEL_COUNT (4)

// --------------------------------------------------------------------------------------------
// Enums

typedef enum dtmcp4728_channel_t
{
    DTMCP4728_CHANNEL_A = 0,
    DTMCP4728_CHANNEL_B = 1,
    DTMCP4728_CHANNEL_C = 2,
    DTMCP4728_CHANNEL_D = 3
} dtmcp4728_channel_t;

typedef enum dtmcp4728_vref_t
{
    DTMCP4728_VREF_VDD = 0,
    DTMCP4728_VREF_INTERNAL = 1
} dtmcp4728_vref_t;

typedef enum dtmcp4728_power_down_t
{
    DTMCP4728_POWER_DOWN_NORMAL = 0,
    DTMCP4728_POWER_DOWN_1K = 1,
    DTMCP4728_POWER_DOWN_100K = 2,
    DTMCP4728_POWER_DOWN_500K = 3
} dtmcp4728_power_down_t;

typedef enum dtmcp4728_gain_t
{
    DTMCP4728_GAIN_X1 = 0,
    DTMCP4728_GAIN_X2 = 1
} dtmcp4728_gain_t;

// --------------------------------------------------------------------------------------------
// Public per-channel config/value

typedef struct dtmcp4728_channel_config_t
{
    dtmcp4728_channel_t channel;
    uint16_t value_12bit;
    dtmcp4728_vref_t vref;
    dtmcp4728_power_down_t power_down;
    dtmcp4728_gain_t gain;
    bool udac;
} dtmcp4728_channel_config_t;

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
// Lifecycle

void
dtmcp4728_espidf_config_init_defaults(dtmcp4728_espidf_config_t* cfg);

dterr_t*
dtmcp4728_espidf_create(dtmcp4728_espidf_t** self_ptr);

dterr_t*
dtmcp4728_espidf_init(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_configure(dtmcp4728_espidf_t* self, const dtmcp4728_espidf_config_t* config);

dterr_t*
dtmcp4728_espidf_attach(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_detach(dtmcp4728_espidf_t* self);

void
dtmcp4728_espidf_dispose(dtmcp4728_espidf_t* self);

// --------------------------------------------------------------------------------------------
// Commands

dterr_t*
dtmcp4728_espidf_fast_write(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t channels[DTMCP4728_CHANNEL_COUNT]);

dterr_t*
dtmcp4728_espidf_multi_write(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t* channel_config);

dterr_t*
dtmcp4728_espidf_sequential_write(dtmcp4728_espidf_t* self,
  dtmcp4728_channel_t start_channel,
  const dtmcp4728_channel_config_t* channel_configs,
  int32_t channel_count);

dterr_t*
dtmcp4728_espidf_single_write_eeprom(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t* channel_config);

dterr_t*
dtmcp4728_espidf_general_call_reset(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_general_call_wakeup(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_general_call_software_update(dtmcp4728_espidf_t* self);

dterr_t*
dtmcp4728_espidf_read_all(dtmcp4728_espidf_t* self, dtmcp4728_channel_config_t channels[DTMCP4728_CHANNEL_COUNT]);

dterr_t*
dtmcp4728_espidf_to_string(dtmcp4728_espidf_t* self, char* buffer, int32_t buffer_size);
