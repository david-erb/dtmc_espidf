#include <dtcore/dterr.h>
#include <dtcore/dtunittest.h>

#include <dtmc_base/dtinterval.h>

#include <dtmc/dtinterval_espidf.h>

#include <dtmc_base_tests.h>
#include <dtmc_espidf_tests.h>

#define TAG "test_dtmc_espidf_dtinterval"

#define TEST_INTERVAL_MS 50

// --------------------------------------------------------------------------------------
static dterr_t*
_create_interval(dtinterval_handle* out_handle)
{
    dterr_t* dterr = NULL;
    dtinterval_espidf_t* o = NULL;

    DTERR_ASSERT_NOT_NULL(out_handle);
    *out_handle = NULL;

    DTERR_C(dtinterval_espidf_create(&o));
    DTERR_C(dtinterval_espidf_configure(o,
      &(dtinterval_espidf_config_t){
        .name = "test",
        .periodic_interval_micros = TEST_INTERVAL_MS * 1000,
      }));

    *out_handle = (dtinterval_handle)o;
    o = NULL;

cleanup:
    if (o != NULL)
        dtinterval_espidf_dispose(o);
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
test_dtmc_espidf_dtinterval_ticks(void)
{
    dterr_t* dterr = NULL;
    dtinterval_handle h = NULL;

    DTERR_C(_create_interval(&h));
    DTERR_C(test_dtmc_base_dtinterval_ticks(h, TEST_INTERVAL_MS));

cleanup:
    dtinterval_dispose(h);
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
test_dtmc_espidf_dtinterval_null_guards(void)
{
    dterr_t* dterr = NULL;
    dtinterval_handle h = NULL;

    DTERR_C(_create_interval(&h));
    DTERR_C(test_dtmc_base_dtinterval_null_guards(h));

cleanup:
    dtinterval_dispose(h);
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
test_dtmc_espidf_dtinterval_callback_error_propagates(void)
{
    dterr_t* dterr = NULL;
    dtinterval_handle h = NULL;

    DTERR_C(_create_interval(&h));
    DTERR_C(test_dtmc_base_dtinterval_callback_error_propagates(h));

cleanup:
    dtinterval_dispose(h);
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
test_dtmc_espidf_dtinterval_accuracy_under_load(void)
{
    dterr_t* dterr = NULL;
    dtinterval_handle h = NULL;

    DTERR_C(_create_interval(&h));
    DTERR_C(test_dtmc_base_dtinterval_accuracy_under_load(h, TEST_INTERVAL_MS));

cleanup:
    dtinterval_dispose(h);
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
test_dtmc_espidf_dtinterval_pause_is_idempotent(void)
{
    dterr_t* dterr = NULL;
    dtinterval_handle h = NULL;

    DTERR_C(_create_interval(&h));
    DTERR_C(test_dtmc_base_dtinterval_pause_is_idempotent(h));

cleanup:
    dtinterval_dispose(h);
    return dterr;
}

// --------------------------------------------------------------------------------------
void
test_dtmc_espidf_dtinterval(DTUNITTEST_SUITE_ARGS)
{
    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtinterval_ticks);
    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtinterval_null_guards);
    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtinterval_callback_error_propagates);
    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtinterval_accuracy_under_load);
    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtinterval_pause_is_idempotent);
}
