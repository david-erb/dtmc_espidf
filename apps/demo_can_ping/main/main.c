#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>

#include <dtmc/dtiox_espidf_canbus.h>

#include <dtmc_base_demos/demo_iox.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtmc_base_demo_iox_t* demo = NULL;
    dtiox_handle iox_handle = NULL;

    // === create the concrete IOX object we need ===
    {
        dtiox_espidf_canbus_t* o = NULL;
        DTERR_C(dtiox_espidf_canbus_create(&o));
        iox_handle = (dtiox_handle)o;
        dtiox_espidf_canbus_config_t c = { 0 };
        c.tx_identifier = 0x100;
        c.bitrate = 500000;
        c.tx_gpio_num = 21;
        c.rx_gpio_num = 22;
        DTERR_C(dtiox_espidf_canbus_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.iox_handle = iox_handle;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(dtmc_base_demo_iox_start(demo));

cleanup:
    // === log and dispose error chain if any ===
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose because start may have left things running
    dtmc_base_demo_iox_dispose(demo);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
