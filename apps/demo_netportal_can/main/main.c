#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtframer_simple.h>
#include <dtmc_base/dtnetportal_iox.h>

// these concrete objects are platform specific
#include <dtmc/dtiox_espidf_canbus.h>

#include <dtmc_base_demos/demo_netportal.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtframer_handle framer_handle = NULL;
    dtsemaphore_handle rx_semaphore_handle = NULL;
    dttasker_handle rx_tasker_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;
    dtsemaphore_handle pong_semaphore_handle = NULL;

    demo_t* demo = NULL;

    // ==== print the currently registered devices ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));

        dtlog_info(TAG, "devices:\n%s", s);

        dtstr_dispose(s);
    }

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

    // === the framer ===
    {
        dtframer_simple_t* o = NULL;
        DTERR_C(dtframer_simple_create(&o));
        framer_handle = (dtframer_handle)o;
        dtframer_simple_config_t c = { 0 };
        DTERR_C(dtframer_simple_configure(o, &c));
    }

    // === the semaphore ===
    {
        dtsemaphore_espidf_t* o = NULL;
        DTERR_C(dtsemaphore_espidf_create(&o));
        rx_semaphore_handle = (dtsemaphore_handle)o;
        dtsemaphore_espidf_config_t c = { 0 };
        c.initial_count = 0;
        DTERR_C(dtsemaphore_espidf_configure(o, &c));
    }

    // === the tasker ===
    {
        dttasker_espidf_t* tasker = NULL;
        DTERR_C(dttasker_espidf_create(&tasker));
        rx_tasker_handle = (dttasker_handle)tasker;
        dttasker_espidf_config_t config = { 0 };
        config.name = "dtiox_rx";
        config.stack_size = 4096;
        config.priority = 6;
        config.core = 0;

        DTERR_C(dttasker_espidf_configure(tasker, &config));
    }

    // === the netportal ===
    {
        dtnetportal_iox_t* o = NULL;
        DTERR_C(dtnetportal_iox_create(&o));
        netportal_handle = (dtnetportal_handle)o;
        dtnetportal_iox_config_t c = { 0 };
        c.iox_handle = iox_handle;
        c.framer_handle = framer_handle;
        c.rx_semaphore_handle = rx_semaphore_handle;
        c.rx_tasker_handle = rx_tasker_handle;
        DTERR_C(dtnetportal_iox_configure(o, &c));
    }

    // === the semaphore for pingpong in the demo to use ===
    {
        dtsemaphore_espidf_t* o = NULL;
        DTERR_C(dtsemaphore_espidf_create(&o));
        pong_semaphore_handle = (dtsemaphore_handle)o;
        dtsemaphore_espidf_config_t c = { 0 };
        c.initial_count = 0;
        DTERR_C(dtsemaphore_espidf_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.netportal_handle = netportal_handle;
        c.pong_semaphore_handle = pong_semaphore_handle;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    int rc = (dterr != NULL) ? -1 : 0;

    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the objects
    dtnetportal_dispose(netportal_handle);
    dttasker_dispose(rx_tasker_handle);
    dtsemaphore_dispose(rx_semaphore_handle);
    dtframer_dispose(framer_handle);
    dtiox_dispose(iox_handle);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
