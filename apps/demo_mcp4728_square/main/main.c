#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

// this concrete object is platform specific
#include <dtmc/dtmcp4728_espidf.h>

#include <dtmc_base_demos/demo_iox.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtmc_base_demo_iox_t* demo = NULL;

    // === create the concrete IOX object we need ===
    {
        dtmcp4728_espidf_t* o = NULL;
        DTERR_C(dtmcp4728_espidf_create(&o));
        iox_handle = (dtiox_handle)o;
        dtmcp4728_espidf_config_t c = { .i2c_port = DTMCP4728_DEFAULT_I2C_PORT,
            .sda_pin = DTMCP4728_DEFAULT_SDA_PIN,
            .scl_pin = DTMCP4728_DEFAULT_SCL_PIN,
            .clock_speed_hz = DTMCP4728_DEFAULT_I2C_CLOCK_HZ,
            .timeout_ms = DTMCP4728_DEFAULT_I2C_TIMEOUT_MS,
            .device_address_7bit = DTMCP4728_DEFAULT_I2C_ADDRESS,
            .enable_pullups = false,
            .install_driver = true };
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

        DTERR_C(dtmcp4728_espidf_sequential_write(o, DTMCP4728_CHANNEL_A, channel_configs, 4));

        dtlog_info(TAG,
          "have set channels A (0x%" PRIu16 ") B (0x%" PRIu16 ") C (0x%" PRIu16 ") D (0x%" PRIu16 ")",
          value_A,
          value_B,
          value_C,
          value_D);
        vTaskDelay(portMAX_DELAY);

        DTERR_C(dtmcp4728_espidf_detach(o));
        dtmcp4728_espidf_dispose(o);
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
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the object
    dtiox_dispose(iox_handle);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
