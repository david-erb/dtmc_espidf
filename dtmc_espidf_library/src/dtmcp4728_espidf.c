// dtmcp4728_espidf.c

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtmc_base_constants.h>
#include <dtmc_base/dtmcp4728.h>

#include <dtmc/dtmc_espidf.h>
#include <dtmc/dtmcp4728_espidf.h>

#define TAG "dtmcp4728_espidf"
// #define dtlog_debug(TAG, ...)

// --------------------------------------------------------------------------------------------
// Concrete type

struct dtmcp4728_espidf_t
{
    DTMCP4728_COMMON_MEMBERS
    dtmcp4728_espidf_config_t config;

    bool is_configured;
    bool is_attached;
    bool driver_installed_by_self;

    i2c_config_t i2c_config;
};

// --------------------------------------------------------------------------------------------

DTMCP4728_INIT_VTABLE(dtmcp4728_espidf);

// --------------------------------------------------------------------------------------------
// Internal declarations

static dterr_t*
dtmcp4728_espidf__validate_config(const dtmcp4728_espidf_config_t* cfg);

static dterr_t*
dtmcp4728_espidf__validate_attached(dtmcp4728_espidf_t* self);

static dterr_t*
dtmcp4728_espidf__write(dtmcp4728_espidf_t* self, uint8_t address_7bit, const uint8_t* bytes, size_t byte_count);

#if false
static dterr_t*
dtmcp4728_espidf__read(dtmcp4728_espidf_t* self, uint8_t address_7bit, uint8_t* bytes, size_t byte_count);
#endif

static dterr_t*
dtmcp4728_espidf__validate_channel(dtmcp4728_channel_t channel);

static dterr_t*
dtmcp4728_espidf__validate_channel_config(const dtmcp4728_channel_config_t* channel_config);

static uint8_t
dtmcp4728_espidf__bool_to_bit(bool value);

static uint8_t
dtmcp4728_espidf__pack_dac_upper_nibble(uint16_t value_12bit);

// --------------------------------------------------------------------------------------------
// Defaults

void
dtmcp4728_espidf_config_init_defaults(dtmcp4728_espidf_config_t* cfg)
{
    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));

    cfg->i2c_port = DTMCP4728_DEFAULT_I2C_PORT;
    cfg->sda_pin = DTMCP4728_DEFAULT_SDA_PIN;
    cfg->scl_pin = DTMCP4728_DEFAULT_SCL_PIN;
    cfg->clock_speed_hz = DTMCP4728_DEFAULT_I2C_CLOCK_HZ;
    cfg->timeout_ms = DTMCP4728_DEFAULT_I2C_TIMEOUT_MS;
    cfg->device_address_7bit = DTMCP4728_DEFAULT_I2C_ADDRESS;
    cfg->enable_pullups = true;
    cfg->install_driver = true;
}

// --------------------------------------------------------------------------------------------
// Lifecycle

dterr_t*
dtmcp4728_espidf_create(dtmcp4728_espidf_t** self_ptr)
{
    dterr_t* dterr = NULL;
    dtmcp4728_espidf_t* self = NULL;

    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtmcp4728_espidf_t), "dtmcp4728_espidf_t", (void**)&self));

    *self_ptr = self;

    DTERR_C(dtmcp4728_espidf_init(self));

cleanup:
    if (dterr)
    {
        dtheaper_free(self);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtmcp4728_espidf_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------------

dterr_t*
dtmcp4728_espidf_init(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_MCP4728_MODEL_ESPIDF;

    DTERR_C(dtmcp4728_set_vtable(self->model_number, &dtmcp4728_espidf_vt));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtmcp4728_espidf_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------------

dterr_t*
dtmcp4728_espidf_configure(dtmcp4728_espidf_t* self, const dtmcp4728_espidf_config_t* config)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    if (self->is_attached)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot configure while attached");
        goto cleanup;
    }

    DTERR_C(dtmcp4728_espidf__validate_config(config));

    self->config = *config;
    self->is_configured = true;

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtmcp4728_espidf_configure failed");
    return dterr;
}

// --------------------------------------------------------------------------------------------

dterr_t*
dtmcp4728_espidf_attach(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    if (!self->is_configured)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtmcp4728_espidf must be configured before attach");
        goto cleanup;
    }

    if (self->is_attached)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtmcp4728_espidf already attached");
        goto cleanup;
    }

    memset(&self->i2c_config, 0, sizeof(self->i2c_config));
    self->i2c_config.mode = I2C_MODE_MASTER;
    self->i2c_config.sda_io_num = self->config.sda_pin;
    self->i2c_config.scl_io_num = self->config.scl_pin;
    self->i2c_config.sda_pullup_en = self->config.enable_pullups ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    self->i2c_config.scl_pullup_en = self->config.enable_pullups ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    self->i2c_config.master.clk_speed = self->config.clock_speed_hz;

    DTMC_ESPIDF_C(i2c_param_config(self->config.i2c_port, &self->i2c_config));

    if (self->config.install_driver)
    {
        DTMC_ESPIDF_C(i2c_driver_install(self->config.i2c_port, I2C_MODE_MASTER, 0, 0, 0));

        self->driver_installed_by_self = true;
    }

    self->is_attached = true;

cleanup:
    if (dterr)
    {
        if (self->driver_installed_by_self)
        {
            i2c_driver_delete(self->config.i2c_port);
            self->driver_installed_by_self = false;
        }

        self->is_attached = false;
    }
    return dterr;
}

// --------------------------------------------------------------------------------------------

dterr_t*
dtmcp4728_espidf_detach(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    if (!self->is_attached)
        goto cleanup;

    if (self->driver_installed_by_self)
    {
        DTMC_ESPIDF_C(i2c_driver_delete(self->config.i2c_port));
        self->driver_installed_by_self = false;
    }

    self->is_attached = false;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------

void
dtmcp4728_espidf_dispose(dtmcp4728_espidf_t* self)
{
    if (!self)
        return;

    if (self->is_attached)
        dtmcp4728_espidf_detach(self);

    memset(self, 0, sizeof(*self));
    dtheaper_free(self);
}

// --------------------------------------------------------------------------------------------
// Commands
//
// Datasheet command families:
// - Fast Write
// - Multi-Write
// - Sequential Write
// - Single Write (Input Register + EEPROM)
// - General Call Reset / Wake-Up / Software Update
//
// read_all() kept as not-implemented stub for now.

// Fast Write:
// 1st transmitted byte after slave address:
//   [C2 C1] = 00
// Then for each channel A..D:
//   byte n:   X X PD1 PD0 D11 D10 D9 D8
//   byte n+1: D7 D6 D5 D4 D3 D2 D1 D0
dterr_t*
dtmcp4728_espidf_fast_write(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t channels[DTMCP4728_CHANNEL_COUNT])
{
    dterr_t* dterr = NULL;
    uint8_t bytes[8];
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(channels);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));

    for (i = 0; i < DTMCP4728_CHANNEL_COUNT; i++)
    {
        DTERR_C(dtmcp4728_espidf__validate_channel_config(&channels[i]));

        bytes[i * 2 + 0] = (uint8_t)((((uint8_t)channels[i].power_down & 0x03) << 4) |
                                     (dtmcp4728_espidf__pack_dac_upper_nibble(channels[i].value_12bit) & 0x0F));

        bytes[i * 2 + 1] = (uint8_t)(channels[i].value_12bit & 0xFF);
    }

    DTERR_C(dtmcp4728_espidf__write(self, self->config.device_address_7bit, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// Multi-Write:
// command byte = 0 1 0 0 0 DAC1 DAC0 UDAC
// next bytes:
//   VREF PD1 PD0 Gx D11 D10 D9 D8
//   D7   D6  D5  D4 D3  D2  D1 D0
dterr_t*
dtmcp4728_espidf_multi_write(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t* channel_config)
{
    dterr_t* dterr = NULL;
    uint8_t bytes[3];

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(channel_config);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__validate_channel_config(channel_config));

    bytes[0] =
      (uint8_t)((0u << 7) | (1u << 6) | (0u << 5) | (0u << 4) | (0u << 3) | (((uint8_t)channel_config->channel & 0x03u) << 1) |
                (dtmcp4728_espidf__bool_to_bit(channel_config->udac) & 0x01u));

    bytes[1] = (uint8_t)((((uint8_t)channel_config->vref & 0x01u) << 7) | (((uint8_t)channel_config->power_down & 0x03u) << 5) |
                         (((uint8_t)channel_config->gain & 0x01u) << 4) |
                         (dtmcp4728_espidf__pack_dac_upper_nibble(channel_config->value_12bit) & 0x0Fu));

    bytes[2] = (uint8_t)(channel_config->value_12bit & 0xFFu);

    DTERR_C(dtmcp4728_espidf__write(self, self->config.device_address_7bit, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// Sequential Write:
// command byte = 0 1 0 1 0 DAC1 DAC0 UDAC
// then repeated per channel from start_channel through end:
//   VREF PD1 PD0 Gx D11 D10 D9 D8
//   D7   D6  D5  D4 D3  D2  D1 D0
dterr_t*
dtmcp4728_espidf_sequential_write(dtmcp4728_espidf_t* self,
  dtmcp4728_channel_t start_channel,
  const dtmcp4728_channel_config_t* channel_configs,
  int32_t channel_count)
{
    dterr_t* dterr = NULL;
    uint8_t bytes[1 + (2 * DTMCP4728_CHANNEL_COUNT)];
    int32_t i;
    int32_t p = 0;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(channel_configs);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__validate_channel(start_channel));

    if (channel_count <= 0 || channel_count > DTMCP4728_CHANNEL_COUNT)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "channel_count must be between 1 and %d", DTMCP4728_CHANNEL_COUNT);
        goto cleanup;
    }

    if (((int32_t)start_channel + channel_count) > DTMCP4728_CHANNEL_COUNT)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "start_channel + channel_count exceeds channel D");
        goto cleanup;
    }

    for (i = 0; i < channel_count; i++)
    {
        DTERR_C(dtmcp4728_espidf__validate_channel_config(&channel_configs[i]));

        if ((int32_t)channel_configs[i].channel != ((int32_t)start_channel + i))
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "channel_configs[%d].channel must match sequential channel order starting from start_channel",
              i);
            goto cleanup;
        }
    }

    bytes[p++] = (uint8_t)((0u << 7) | (1u << 6) | (0u << 5) | (1u << 4) | (0u << 3) | (((uint8_t)start_channel & 0x03u) << 1) |
                           (dtmcp4728_espidf__bool_to_bit(channel_configs[0].udac) & 0x01u));

    for (i = 0; i < channel_count; i++)
    {
        bytes[p++] = (uint8_t)((((uint8_t)channel_configs[i].vref & 0x01u) << 7) |
                               (((uint8_t)channel_configs[i].power_down & 0x03u) << 5) |
                               (((uint8_t)channel_configs[i].gain & 0x01u) << 4) |
                               (dtmcp4728_espidf__pack_dac_upper_nibble(channel_configs[i].value_12bit) & 0x0Fu));

        bytes[p++] = (uint8_t)(channel_configs[i].value_12bit & 0xFFu);
    }

    DTERR_C(dtmcp4728_espidf__write(self, self->config.device_address_7bit, bytes, (size_t)p));

cleanup:
    return dterr;
}

// Single Write EEPROM:
// command byte = 0 1 0 1 1 DAC1 DAC0 UDAC
// next bytes:
//   VREF PD1 PD0 Gx D11 D10 D9 D8
//   D7   D6  D5  D4 D3  D2  D1 D0
dterr_t*
dtmcp4728_espidf_single_write_eeprom(dtmcp4728_espidf_t* self, const dtmcp4728_channel_config_t* channel_config)
{
    dterr_t* dterr = NULL;
    uint8_t bytes[3];

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(channel_config);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__validate_channel_config(channel_config));

    bytes[0] =
      (uint8_t)((0u << 7) | (1u << 6) | (0u << 5) | (1u << 4) | (1u << 3) | (((uint8_t)channel_config->channel & 0x03u) << 1) |
                (dtmcp4728_espidf__bool_to_bit(channel_config->udac) & 0x01u));

    bytes[1] = (uint8_t)((((uint8_t)channel_config->vref & 0x01u) << 7) | (((uint8_t)channel_config->power_down & 0x03u) << 5) |
                         (((uint8_t)channel_config->gain & 0x01u) << 4) |
                         (dtmcp4728_espidf__pack_dac_upper_nibble(channel_config->value_12bit) & 0x0Fu));

    bytes[2] = (uint8_t)(channel_config->value_12bit & 0xFFu);

    DTERR_C(dtmcp4728_espidf__write(self, self->config.device_address_7bit, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// General Call Reset = 0x00 address, then 0x06
dterr_t*
dtmcp4728_espidf_general_call_reset(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;
    const uint8_t bytes[1] = { 0x06 };

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__write(self, 0x00, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// General Call Wake-Up = 0x00 address, then 0x09
dterr_t*
dtmcp4728_espidf_general_call_wakeup(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;
    const uint8_t bytes[1] = { 0x09 };

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__write(self, 0x00, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// General Call Software Update = 0x00 address, then 0x08
dterr_t*
dtmcp4728_espidf_general_call_software_update(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;
    const uint8_t bytes[1] = { 0x08 };

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));
    DTERR_C(dtmcp4728_espidf__write(self, 0x00, bytes, sizeof(bytes)));

cleanup:
    return dterr;
}

// Readback exists on the device, but decoding the returned register stream is left
// for a second pass so we do not accidentally encode the wrong assumptions here.
dterr_t*
dtmcp4728_espidf_read_all(dtmcp4728_espidf_t* self, dtmcp4728_channel_config_t channels[DTMCP4728_CHANNEL_COUNT])
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(channels);

    DTERR_C(dtmcp4728_espidf__validate_attached(self));

    (void)channels;

    dterr = dterr_new(DTERR_NOTIMPL, DTERR_LOC, NULL, "dtmcp4728_espidf_read_all is not implemented yet");

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
// Convert to string

dterr_t*
dtmcp4728_espidf_to_string(dtmcp4728_espidf_t* self, char* buffer, int32_t buffer_size)
{
    dterr_t* dterr = NULL;
    int32_t offset = 0;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    if (buffer_size <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "buffer_size must be > 0");
        goto cleanup;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "dtmcp4728_espidf");

    offset += snprintf(buffer + offset,
      buffer_size - offset,
      " port=%d sda=%d scl=%d addr=0x%02x hz=%lu configured=%d attached=%d",
      (int)self->config.i2c_port,
      (int)self->config.sda_pin,
      (int)self->config.scl_pin,
      (unsigned int)self->config.device_address_7bit,
      (unsigned long)self->config.clock_speed_hz,
      self->is_configured ? 1 : 0,
      self->is_attached ? 1 : 0);

    buffer[buffer_size - 1] = '\0';

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
// Internal helpers

static dterr_t*
dtmcp4728_espidf__validate_config(const dtmcp4728_espidf_config_t* cfg)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(cfg);

    if (cfg->clock_speed_hz <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "clock_speed_hz must be > 0");
        goto cleanup;
    }

    if (cfg->timeout_ms <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "timeout_ms must be > 0");
        goto cleanup;
    }

    if (cfg->device_address_7bit > 0x7F)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "device_address_7bit must be <= 0x7F");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------

static dterr_t*
dtmcp4728_espidf__validate_attached(dtmcp4728_espidf_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    if (!self->is_configured)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtmcp4728_espidf is not configured");
        goto cleanup;
    }

    if (!self->is_attached)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtmcp4728_espidf is not attached");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------

static dterr_t*
dtmcp4728_espidf__validate_channel(dtmcp4728_channel_t channel)
{
    dterr_t* dterr = NULL;

    if ((int32_t)channel < (int32_t)DTMCP4728_CHANNEL_A || (int32_t)channel > (int32_t)DTMCP4728_CHANNEL_D)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "channel must be between A and D");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------

static dterr_t*
dtmcp4728_espidf__validate_channel_config(const dtmcp4728_channel_config_t* channel_config)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(channel_config);

    DTERR_C(dtmcp4728_espidf__validate_channel(channel_config->channel));

    if (channel_config->value_12bit > 0x0FFF)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "value_12bit must be <= 0x0FFF");
        goto cleanup;
    }

    if ((int32_t)channel_config->vref < 0 || (int32_t)channel_config->vref > 1)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "vref is out of range");
        goto cleanup;
    }

    if ((int32_t)channel_config->power_down < 0 || (int32_t)channel_config->power_down > 3)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "power_down is out of range");
        goto cleanup;
    }

    if ((int32_t)channel_config->gain < 0 || (int32_t)channel_config->gain > 1)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "gain is out of range");
        goto cleanup;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------

static dterr_t*
dtmcp4728_espidf__write(dtmcp4728_espidf_t* self, uint8_t address_7bit, const uint8_t* bytes, size_t byte_count)
{
    dterr_t* dterr = NULL;
    i2c_cmd_handle_t cmd = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(bytes);

    if (byte_count <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "byte_count must be > 0");
        goto cleanup;
    }

    cmd = i2c_cmd_link_create();
    if (!cmd)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "i2c_cmd_link_create failed");
        goto cleanup;
    }

    DTMC_ESPIDF_C(i2c_master_start(cmd));

    DTMC_ESPIDF_C(i2c_master_write_byte(cmd, (uint8_t)((address_7bit << 1) | I2C_MASTER_WRITE), true));

    DTMC_ESPIDF_C(i2c_master_write(cmd, (uint8_t*)bytes, byte_count, true));

    DTMC_ESPIDF_C(i2c_master_stop(cmd));

    DTMC_ESPIDF_C(i2c_master_cmd_begin(self->config.i2c_port, cmd, pdMS_TO_TICKS(self->config.timeout_ms)));

cleanup:
    if (cmd)
        i2c_cmd_link_delete(cmd);

    return dterr;
}

#if false
// --------------------------------------------------------------------------------------------

static dterr_t*
dtmcp4728_espidf__read(dtmcp4728_espidf_t* self, uint8_t address_7bit, uint8_t* bytes, size_t byte_count)
{
    dterr_t* dterr = NULL;
    i2c_cmd_handle_t cmd = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(bytes);

    if (byte_count <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "byte_count must be > 0");
        goto cleanup;
    }

    cmd = i2c_cmd_link_create();
    if (!cmd)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "i2c_cmd_link_create failed");
        goto cleanup;
    }

    DTMC_ESPIDF_C(i2c_master_start(cmd));

    DTMC_ESPIDF_C(i2c_master_write_byte(cmd, (uint8_t)((address_7bit << 1) | I2C_MASTER_READ), true));

    if (byte_count > 1)
    {
        DTMC_ESPIDF_C(i2c_master_read(cmd, bytes, byte_count - 1, I2C_MASTER_ACK));
    }

    DTMC_ESPIDF_C(i2c_master_read_byte(cmd, &bytes[byte_count - 1], I2C_MASTER_NACK));

    DTMC_ESPIDF_C(i2c_master_stop(cmd));

    DTMC_ESPIDF_C(i2c_master_cmd_begin(self->config.i2c_port, cmd, pdMS_TO_TICKS(self->config.timeout_ms)));

cleanup:
    if (cmd)
        i2c_cmd_link_delete(cmd);

    return dterr;
}
#endif

// --------------------------------------------------------------------------------------------

static uint8_t
dtmcp4728_espidf__bool_to_bit(bool value)
{
    return value ? 1u : 0u;
}

// --------------------------------------------------------------------------------------------

static uint8_t
dtmcp4728_espidf__pack_dac_upper_nibble(uint16_t value_12bit)
{
    return (uint8_t)((value_12bit >> 8) & 0x0F);
}