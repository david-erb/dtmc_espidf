#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

// this concrete object is platform specific
#include <dtmc/dtadc_espidf_oneshot.h>

#include <dtmc_base_demos/demo_adc_print.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtadc_handle adc_handle = NULL;
    demo_t* demo = NULL;

    // === create the concrete adc object we need ===
    {
        dtadc_espidf_oneshot_t* o = NULL;
        DTERR_C(dtadc_espidf_oneshot_create(&o));
        adc_handle = (dtadc_handle)o;
        dtadc_espidf_oneshot_config_t c = { 0 };
        dtadc_espidf_oneshot_config_init_defaults(&c);
        DTERR_C(dtadc_espidf_oneshot_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.adc_handle = adc_handle;
        c.scan_timeout_ms = 10000;
        c.max_scan_count = 10;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the object
    dtadc_dispose(adc_handle);

    dtlog_info(TAG, "demo finished");

    // Wait indefinitely to prevent the program from exiting.
    while (true)
    {
        dtruntime_sleep_milliseconds(1000);
    }
}
