#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_timer.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dteventlogger.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>
#include <dtcore/dtunittest.h>

#include <dtmc_base/dttasker_registry.h>

#include <dtmc_base/dtdotstar.h>
#include <dtmc_base/dtinterval.h>
#include <dtmc_base/dtinterval_scheduled.h>

#include <dtmc/dtdotstar_espidf.h>

#include <dtmc_base/dtcpu.h>
#include <dtmc_base/dtinterval.h>
#include <dtmc_base/dtruntime.h>

#define TAG "test_dtdotstar"

// GPIO and driver config for SPI
#define LED_COUNT 30
#define DOTSTAR_SPI_HOST SPI3_HOST
#define DOTSTAR_MOSI 23
#define DOTSTAR_SCK 18

typedef struct test_context_t
{
    dtdotstar_handle dotstar_handle;
    dtinterval_handle interval_handle;

    dtdotstar_lumen_t reflector_lumens[LED_COUNT];

    int call_count;

    dtcpu_t cpu;

    dteventlogger_t event_logger;
    dteventlogger_item1_t log_item;

    dttasker_registry_t registry;

} test_context_t;

#define SCHEDULE_INTERVAL_MILLISECONDS 1
#define LOOP_COUNT 20

// --------------------------------------------------------------------------------------
dterr_t*
scheduled_interval_callback(void* context, int* should_pause)
{
    dterr_t* dterr = NULL;

    test_context_t* self = (test_context_t*)context;

    DTERR_C(dtdotstar_dither(self->dotstar_handle, self->reflector_lumens));
    DTERR_C(dtdotstar_transmit(self->dotstar_handle));

    dtcpu_mark(&self->cpu);
    self->log_item.value1 = self->call_count;
    self->log_item.value2 = (int32_t)dtcpu_elapsed_microseconds(&self->cpu);
    DTERR_C(dteventlogger_append(&self->event_logger, &self->log_item));

    self->call_count++;
    if (self->call_count >= LOOP_COUNT)
        *should_pause = 1;

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "scheduled interval callback failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
_setup(test_context_t* self)
{
    dterr_t* dterr = NULL;
    dtdotstar_handle dotstar_handle = NULL;

    // early reference to the dtmc_base to help linker due to circular dependency
    dtruntime_now_milliseconds();

    for (int i = 0; i < LED_COUNT; i++)
    {
        if (i == 26)
        {
            self->reflector_lumens[i].R = 255;
            self->reflector_lumens[i].G = 255;
            self->reflector_lumens[i].B = 255;
            self->reflector_lumens[i].Flux = 31;
        }
        if (i == 29)
        {
            self->reflector_lumens[i].R = 25;
            self->reflector_lumens[i].G = 25;
            self->reflector_lumens[i].B = 25;
            self->reflector_lumens[i].Flux = 31;
        }
    }

    {
        dtdotstar_espidf_t* o = NULL;

        // Create instance
        DTERR_C(dtdotstar_espidf_create(&o));
        dotstar_handle = (dtdotstar_handle)o;

        // TODO: Parameterize dotstar configuration in test_dtmc_espidf_dtdotstar_basic().
        dtdotstar_espidf_config_t c = {
            .led_count = LED_COUNT,
            .mosi_io = DOTSTAR_MOSI,
            .sclk_io = DOTSTAR_SCK,
            .spi_host = DOTSTAR_SPI_HOST,
        };
        DTERR_C(dtdotstar_espidf_configure(o, &c));
    }

    // set the callback for when an SPI transaction completes (must set this before connect)
    DTERR_C(dtdotstar_set_post_cb(dotstar_handle, NULL, NULL));

    {
        dtinterval_scheduled_t* o = NULL;

        // Create instance
        DTERR_C(dtinterval_scheduled_create(&o));
        self->interval_handle = (dtinterval_handle)o;

        dtinterval_scheduled_config_t c = { 0 };
        c.interval_milliseconds = SCHEDULE_INTERVAL_MILLISECONDS;
        DTERR_C(dtinterval_scheduled_configure(o, &c));

        DTERR_C(dtinterval_set_callback(self->interval_handle, &scheduled_interval_callback, self));
    }

    DTERR_C(dtdotstar_connect(dotstar_handle));

    DTERR_C(dteventlogger_init(&self->event_logger, 10, sizeof(dteventlogger_item1_t)));

    DTERR_C(dttasker_registry_init(&self->registry));

    self->dotstar_handle = dotstar_handle;

cleanup:

    if (dterr != NULL)
    {
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unble to setup for dotstar tests");
    }
    return dterr;
}
// --------------------------------------------------------------------------------------
static void
_dispose(test_context_t* self)
{
    if (self == NULL)
        return;

    dttasker_registry_dispose(&self->registry);

    dteventlogger_dispose(&self->event_logger);

    dtdotstar_dispose(self->dotstar_handle);

    self->dotstar_handle = NULL;
    memset(self, 0, sizeof(*self));
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_sys_overhead(void)
{
    dterr_t* dterr = NULL;

    int64_t timer_get_time[LOOP_COUNT];

    dteventlogger_t event_logger = { 0 };
    DTERR_C(dteventlogger_init(&event_logger, 10, sizeof(dteventlogger_item1_t)));

    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        timer_get_time[loop] = esp_timer_get_time();
    }

    dteventlogger_item1_t log_item = { 0 };
    for (int loop = 1; loop < LOOP_COUNT; loop++)
    {
        log_item.value1 = loop;
        log_item.value2 = (int32_t)(timer_get_time[loop] - timer_get_time[loop - 1]);
        DTERR_C(dteventlogger_append(&event_logger, &log_item));
    }

    dteventlogger_log_item1(TAG, &event_logger, __func__, "loop", "microseconds");

cleanup:

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_mark_overhead(void)
{
    dterr_t* dterr = NULL;

    dtcpu_microseconds_t elapsed[LOOP_COUNT];

    dteventlogger_t event_logger = { 0 };
    DTERR_C(dteventlogger_init(&event_logger, 10, sizeof(dteventlogger_item1_t)));

    dtcpu_t cpu;
    dtcpu_mark(&cpu);
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        dtcpu_mark(&cpu);
        elapsed[loop] = dtcpu_elapsed_microseconds(&cpu);
    }

    dteventlogger_item1_t log_item = { 0 };
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        log_item.value1 = loop;
        log_item.value2 = (int32_t)elapsed[loop];
        DTERR_C(dteventlogger_append(&event_logger, &log_item));
    }

    dteventlogger_log_item1(TAG, &event_logger, __func__, "loop", "microseconds");

cleanup:

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_eventlogger_overhead(void)
{
    dterr_t* dterr = NULL;

    dteventlogger_t event_logger = { 0 };
    DTERR_C(dteventlogger_init(&event_logger, 10, sizeof(dteventlogger_item1_t)));

    dteventlogger_item1_t log_item = { 0 };

    dtcpu_t cpu;
    dtcpu_mark(&cpu);
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        dtcpu_mark(&cpu);
        log_item.value1 = loop;
        log_item.value2 = (int32_t)dtcpu_elapsed_microseconds(&cpu);
        DTERR_C(dteventlogger_append(&event_logger, &log_item));
    }

    dteventlogger_log_item1(TAG, &event_logger, __func__, "loop", "microseconds");

cleanup:

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_single(void)
{
    dterr_t* dterr = NULL;
    test_context_t context = { 0 };
    test_context_t* self = &context;

    DTERR_C(_setup(self));

    DTERR_C(dtdotstar_dither(self->dotstar_handle, self->reflector_lumens));
    DTERR_C(dtdotstar_transmit(self->dotstar_handle));

cleanup:
    _dispose(self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_dither_timing(void)
{
    dterr_t* dterr = NULL;
    test_context_t context = { 0 };
    test_context_t* self = &context;

    DTERR_C(_setup(self));

    dteventlogger_item1_t log_item = { 0 };

    dtcpu_t cpu;
    dtcpu_mark(&cpu);
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        DTERR_C(dtdotstar_dither(self->dotstar_handle, self->reflector_lumens));
        dtcpu_mark(&cpu);
        log_item.value1 = loop;
        log_item.value2 = (int32_t)dtcpu_elapsed_microseconds(&cpu);
        DTERR_C(dteventlogger_append(&self->event_logger, &log_item));
    }

    dteventlogger_log_item1(TAG, &self->event_logger, __func__, "loop", "microseconds");

cleanup:
    _dispose(self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_transmit_timing(void)
{
    dterr_t* dterr = NULL;
    test_context_t context = { 0 };
    test_context_t* self = &context;

    DTERR_C(_setup(self));

    dteventlogger_item1_t log_item = { 0 };

    DTERR_C(dtdotstar_dither(self->dotstar_handle, self->reflector_lumens));

    dtcpu_t cpu;
    dtcpu_mark(&cpu);
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        DTERR_C(dtdotstar_transmit(self->dotstar_handle));
        dtcpu_mark(&cpu);
        log_item.value1 = loop;
        log_item.value2 = (int32_t)dtcpu_elapsed_microseconds(&cpu);
        DTERR_C(dteventlogger_append(&self->event_logger, &log_item));
    }

    dteventlogger_log_item1(TAG, &self->event_logger, __func__, "loop", "microseconds");

cleanup:
    _dispose(self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
test_dtmc_espidf_dtdotstar_scheduled(void)
{
    dterr_t* dterr = NULL;
    test_context_t context = { 0 };
    test_context_t* self = &context;

    DTERR_C(_setup(self));
    dtcpu_mark(&self->cpu);

    DTERR_C(dtruntime_register_tasks(&self->registry));

    DTERR_C(dtinterval_start(self->interval_handle));

    DTERR_C(dtruntime_register_tasks(&self->registry));

    dteventlogger_log_item1(TAG, &self->event_logger, __func__, "loop", "microseconds");

    {
        char* s = NULL;
        DTERR_C(dttasker_registry_format_as_table(&self->registry, &s));
        dtlog_info(TAG, "tasks:\n%s", s);
        dtstr_dispose(s);
    }

cleanup:

    _dispose(self);

    return dterr;
}

// --------------------------------------------------------------------------------------------
// Runs all tests
void
test_dtmc_espidf_dtdotstar(DTUNITTEST_SUITE_ARGS)
{
    // DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_sys_overhead);
    // DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_mark_overhead);
    // DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_eventlogger_overhead);
    // DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_dither_timing);
    // DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_transmit_timing);

    DTUNITTEST_RUN_TEST(test_dtmc_espidf_dtdotstar_scheduled);
}
