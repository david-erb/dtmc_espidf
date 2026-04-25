
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtcore_tests.h>
#include <dtmc_base_tests.h>
#include <dtmc_espidf_tests.h>

#define TAG "app_main"

// -------------------------------------------------------------------------------
void
app_main(void)
{
    int fail_count = 0;

    dterr_t* dterr = NULL;

    // local tests
    dterr = test_dtmc_espidf_dtdotstar();
    if (dterr != NULL)
    {
        dterr_dispose(dterr);
        fail_count++;
    }

    if (fail_count > 0)
    {
        printf("\n\n%d test%s FAIL\n", fail_count, fail_count > 1 ? "s" : "");
    }
    else
    {
        printf("\n\nall tests PASS\n");
    }

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}