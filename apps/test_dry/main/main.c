#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtcore_tests.h>

#include <dtmc_base/dtruntime.h>
#include <dtmc_base_tests.h>

#include <dtmc_espidf_tests.h>

#define TAG "app_main"

// -------------------------------------------------------------------------------
void
app_main(void)
{

    dtunittest_control_t unittest_control = { 0 };
    unittest_control.should_print_suites = true;
    unittest_control.should_print_tests = false;
    unittest_control.should_print_errors = true;

    // if (unittest_control.pattern == NULL)
    //     unittest_control.pattern = "dtinterval";

    test_dtcore_matching(&unittest_control);

    test_dtmc_base_matching(&unittest_control);

    test_dtmc_espidf_dry_matching(&unittest_control);

    // print summary as final line of test output
    dtunittest_print_final(&unittest_control);

    printf(DTUNITTEST_FINAL_PRINTF_SENTINEL);

    while (true)
    {
        dtruntime_sleep_milliseconds(1000);
    }
}