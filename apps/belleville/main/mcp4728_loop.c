#include <stdint.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dttimeseries.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtcpu.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttimeseries.h>

// this concrete object is platform specific
#include <dtmc/dtmcp4728_espidf.h>

#include "main.h"

#define TAG "mcp4728_loop"

// --------------------------------------------------------------------------------------
extern dterr_t*
main_mcp4728_entrypoint(void* context, dttasker_handle tasker_handle)
{
    main_t* self = (main_t*)context;
    dterr_t* dterr = NULL;
    void* mcp4728_handle = NULL;

    // === create the concrete MCP4728 object we need ===

    dtmcp4728_espidf_t* o = NULL;
    DTERR_C(dtmcp4728_espidf_create(&o));
    mcp4728_handle = o;
    dtmcp4728_espidf_config_t c = { //
        .i2c_port = DTMCP4728_DEFAULT_I2C_PORT,
        .sda_pin = DTMCP4728_DEFAULT_SDA_PIN,
        .scl_pin = DTMCP4728_DEFAULT_SCL_PIN,
        .clock_speed_hz = DTMCP4728_DEFAULT_I2C_CLOCK_HZ,
        .timeout_ms = DTMCP4728_DEFAULT_I2C_TIMEOUT_MS,
        .device_address_7bit = DTMCP4728_DEFAULT_I2C_ADDRESS,
        .enable_pullups = false,
        .install_driver = true
    };
    DTERR_C(dtmcp4728_espidf_configure(o, &c));

    DTERR_C(dtmcp4728_espidf_attach(o));
    char tmp[128];
    DTERR_C(dtmcp4728_espidf_to_string(o, tmp, sizeof(tmp)));
    dtlog_debug(TAG, "attached: %s", tmp);

    uint16_t value_A = 0xfff;
    uint16_t value_B = 0xa00;
    uint16_t value_C = 0x500;
    uint16_t value_D = 0x000;

    dtmcp4728_channel_config_t channel_configs[4] = { //
        { .channel = DTMCP4728_CHANNEL_A,
          .value_12bit = value_A,
          .vref = DTMCP4728_VREF_VDD,
          .power_down = DTMCP4728_POWER_DOWN_NORMAL,
          .gain = DTMCP4728_GAIN_X1,
          .udac = false },
        { .channel = DTMCP4728_CHANNEL_B,
          .value_12bit = value_B,
          .vref = DTMCP4728_VREF_VDD,
          .power_down = DTMCP4728_POWER_DOWN_NORMAL,
          .gain = DTMCP4728_GAIN_X1,
          .udac = false },
        { .channel = DTMCP4728_CHANNEL_C,
          .value_12bit = value_C,
          .vref = DTMCP4728_VREF_VDD,
          .power_down = DTMCP4728_POWER_DOWN_NORMAL,
          .gain = DTMCP4728_GAIN_X1,
          .udac = false },
        { .channel = DTMCP4728_CHANNEL_D,
          .value_12bit = value_D,
          .vref = DTMCP4728_VREF_VDD,
          .power_down = DTMCP4728_POWER_DOWN_NORMAL,
          .gain = DTMCP4728_GAIN_X1,
          .udac = false }
    };

    DTERR_C(dttasker_ready(tasker_handle));

    dtcpu_microseconds_t microseconds = dtruntime_now_milliseconds() * 1000;
    while (true)
    {
        for (int i = 0; i < MAIN_MCP4728_CHANNEL_COUNT; i++)
        {
            double value;
            DTERR_C(dttimeseries_read(self->config.timeseries_handles[i], microseconds, &value));

            value = 0.5 + 4095.0 * value; // scale to 12 bits
            if (value > 4095.0)
            {
                value = 4095.0;
            }
            else if (value < 0.0)
            {
                value = 0.0;
            }

            uint16_t value_12bit = (uint16_t)(value);

            channel_configs[i].value_12bit = value_12bit;
        }

        DTERR_C(dtmcp4728_espidf_sequential_write(o, DTMCP4728_CHANNEL_A, channel_configs, 4));

        dtlog_info(TAG,
          "have set channels A (0x%" PRIu16 ") B (0x%" PRIu16 ") C (0x%" PRIu16 ") D (0x%" PRIu16 ")",
          channel_configs[0].value_12bit,
          channel_configs[1].value_12bit,
          channel_configs[2].value_12bit,
          channel_configs[3].value_12bit);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // === create and configure the demo instance ===
    // {
    //     DTERR_C(dtmc_base_demo_iox_create(&demo));
    //     dtmc_base_demo_iox_config_t c = { 0 };
    //     c.iox_handle = iox_handle;
    //     c.node_name = "dtmc_espidf/uart1";
    //     DTERR_C(dtmc_base_demo_iox_configure(demo, &c));
    // }

    // // === start the demo ===
    // DTERR_C(dtmc_base_demo_iox_start(demo));

cleanup:
    dterr_append(dterr, dtmcp4728_espidf_detach(mcp4728_handle));

    dtmcp4728_espidf_dispose(mcp4728_handle);

    return dterr;
}
