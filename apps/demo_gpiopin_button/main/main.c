#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtgpiopin.h>
#include <dtmc_base_demos/demo_gpiopin_button.h>

#include <dtmc/dtgpiopin_espidf.h>

#include <dtmc_base/dtcpu.h>

#define TAG "main"

// some local macros referring to the current demo to make it a bit easier to read the code
#define demo_t dtmc_base_demo_gpiopin_button_t
#define demo_config_t dtmc_base_demo_gpiopin_button_config_t
#define demo_create dtmc_base_demo_gpiopin_button_create
#define demo_configure dtmc_base_demo_gpiopin_button_configure
#define demo_start dtmc_base_demo_gpiopin_button_start
#define demo_dispose dtmc_base_demo_gpiopin_button_dispose

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtgpiopin_handle gpiopin_handle = NULL;
    demo_t* demo = NULL;

    // === create the concrete GPIO pin instance used in the demo ===
    {
        dtgpiopin_espidf_t* o = NULL;
        DTERR_C(dtgpiopin_espidf_create(&o));
        gpiopin_handle = (dtgpiopin_handle)o;

        dtgpiopin_espidf_config_t c = { 0 };
        c.pin_number = 26;
        c.mode = DTGPIOPIN_MODE_INPUT;
        c.pull = DTGPIOPIN_PULL_UP;
        DTERR_C(dtgpiopin_espidf_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.gpiopin_handle = gpiopin_handle;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    // log and dispose error chain (if any)
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo and its resources
    demo_dispose(demo);

    // dispose the GPIO pin instance
    dtgpiopin_dispose(gpiopin_handle);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
