
#include <stdio.h>
#include <string.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtledger.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>
#include <dtcore/dtunittest.h>

#include <dtmc_base/dtbufferqueue.h>

#include <dtmc_espidf_tests.h>

#define TAG "test_dtmc_espidf_most_matching"

// -------------------------------------------------------------------------------
void
test_dtmc_espidf_dry_matching(DTUNITTEST_SUITE_ARGS)
{
    // ledgers we will check at end of each test
    dtledger_t* ledgers[10] = { 0 };
    {
        int i = 0;
        // check dterr before dtstr since dterr may have dtstr allocations
        ledgers[i++] = dterr_ledger;
        ledgers[i++] = dtstr_ledger;
        ledgers[i++] = dtbuffer_ledger;
        ledgers[i++] = dtheaper_ledger;
    }

    unittest_control->ledgers = ledgers;

    DTUNITTEST_RUN_SUITE(test_dtmc_espidf_dtinterval);

    unittest_control->test_setup = NULL;
    unittest_control->test_teardown = NULL;
}
