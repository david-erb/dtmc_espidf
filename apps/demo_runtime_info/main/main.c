
#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base_demos/demo_runtime_info.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    demo_t* demo = NULL;

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    // === log and dispose error chain if any ===
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose because start may have left things running
    demo_dispose(demo);

    dtlog_info(TAG, "%s finished", demo_name);

    // Wait indefinitely to prevent the program from exiting.
    while (true)
    {
        dtruntime_sleep_milliseconds(1000);
    }
}
