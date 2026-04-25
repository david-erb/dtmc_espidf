#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>
#include <dtcore/dtkvp.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtparse.h>
#include <dtcore/dtstr.h>
#include <dtcore/dttimeout.h>

#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtnetportal.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtframer_simple.h>
#include <dtmc_base/dtnetportal_iox.h>

#include <dtmc_base/dtcpu.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttimeseries.h>
#include <dtmc_base/dttimeseries_steady.h>

// these concrete objects are platform specific
#include <dtmc/dtiox_espidf_uart.h>
#include <dtmc/dtmcp4728_espidf.h>

#include <dtmc_base_demos/demo_iox.h>

#include "main.h"

#define TAG "main"

// forward declaration of receive callback
static dterr_t*
_receive_callback(void* opaque_self, const char* topic, dtbuffer_t* buffer);

// -----------------------------------------------------------------------------
// helper to log and dispose errors in one step

static dterr_t*
log_and_dispose(dterr_t* dterr)
{
    if (dterr != NULL)
    {
        dtlog_dterr(TAG, dterr);
        dterr_dispose(dterr);
        dterr = NULL;
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    main_t _self = { 0 }, *self = &_self;
    dtkvp_list_t _kvp_list = { 0 }, *kvp_list = &_kvp_list;
    dttasker_handle mcp4728_tasker_handle = NULL;
    dttasker_handle tasker_handles[10] = { NULL };
    int tasker_handle_count = 0;
    dtiox_handle iox_handle = NULL;
    dtframer_handle framer_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;

    // create timeseries for each channel of the MCP4728
    DTERR_C(dtkvp_list_init(kvp_list));
    for (int i = 0; i < MAIN_MCP4728_CHANNEL_COUNT; i++)
    {
        dttimeseries_steady_t* o = NULL;
        DTERR_C(dttimeseries_steady_create(&o));
        char value[32];
        snprintf(value, sizeof(value), "%0.3f", 0.0);
        DTERR_C(dtkvp_list_set(kvp_list, "value", value));
        DTERR_C(dttimeseries_steady_configure(o, kvp_list));
        self->config.timeseries_handles[i] = (dttimeseries_handle)o;
    }

    // make a task to run the MCP4728 control loop in
    {
        dttasker_config_t c = { 0 };
        c.name = "mcp4728_tasker";
        c.tasker_entry_point_fn = main_mcp4728_entrypoint;
        c.tasker_entry_point_arg = self;
        c.stack_size = 4096;
        c.priority = DTTASKER_PRIORITY_NORMAL_HIGH;
        c.core = 0;
        DTERR_C(dttasker_create(&mcp4728_tasker_handle, &c));
        tasker_handles[tasker_handle_count++] = mcp4728_tasker_handle;
    }

    // === create the concrete IOX object we need ===
    {
        dtiox_espidf_uart_t* o = NULL;
        DTERR_C(dtiox_espidf_uart_create(&o));
        iox_handle = (dtiox_handle)o;
        dtiox_espidf_uart_config_t c = { 0 };
        c.uart_port_num = 1;
        c.tx_pin = 17;
        c.rx_pin = 16;
        c.rts_pin = -1;
        c.cts_pin = -1;
        c.uart_config.baudrate = 115200;
        c.uart_config.data_bits = DTUART_DATA_BITS_8;
        c.uart_config.parity = DTUART_PARITY_NONE;
        c.uart_config.stop_bits = DTUART_STOPBITS_1;
        c.uart_config.flow = DTUART_FLOW_NONE;
        DTERR_C(dtiox_espidf_uart_configure(o, &c));
    }

    // === the framer ===
    {
        dtframer_simple_t* o = NULL;
        DTERR_C(dtframer_simple_create(&o));
        framer_handle = (dtframer_handle)o;
        dtframer_simple_config_t c = { 0 };
        DTERR_C(dtframer_simple_configure(o, &c));
    }

    // === the netportal ===
    {
        dtnetportal_iox_t* o = NULL;
        DTERR_C(dtnetportal_iox_create(&o));
        netportal_handle = (dtnetportal_handle)o;
        dtnetportal_iox_config_t c = { 0 };
        c.iox_handle = iox_handle;
        c.framer_handle = framer_handle;
        DTERR_C(dtnetportal_iox_configure(o, &c));
    }

    DTERR_C(dttasker_start(mcp4728_tasker_handle));

    // activate the netportal
    DTERR_C(dtnetportal_activate(netportal_handle));

    // subscribe to topic and take action when the callback is invoked
    DTERR_C(dtnetportal_subscribe(netportal_handle, MAIN_NETPORTAL_TOPIC, self, _receive_callback));

    while (true)
    {
        dtruntime_sleep_milliseconds(1000);

        for (int i = 0; i < tasker_handle_count; i++)
        {
            dttasker_info_t info = { 0 };
            DTERR_C(dttasker_get_info(tasker_handles[i], &info));
            dterr = info.dterr;
            if (dterr)
            {
                dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "task \"%s\" has error", info.name);
                goto cleanup;
            }
            if (info.status != RUNNING)
            {

                dterr = dterr_new(DTERR_FAIL,
                  DTERR_LOC,
                  NULL,
                  "task \"%s\" is not RUNNING: %s",
                  info.name,
                  dttasker_state_to_string(info.status));
                goto cleanup;
            }
        }
    }

cleanup:
    dterr = log_and_dispose(dterr);

    for (int i = tasker_handle_count - 1; i >= 0; i--)
    {
        if (tasker_handles[i])
        {
            dttasker_info_t info = { 0 };
            DTERR_C(dttasker_get_info(tasker_handles[i], &info));
            dtlog_info(TAG, "stopping task \"%s\"...", info.name);
            log_and_dispose(dttasker_stop(tasker_handles[i]));

            dtlog_info(TAG, "joining task \"%s\"...", info.name);
            log_and_dispose(dttasker_join(tasker_handles[i], 2000, NULL));

            dtlog_info(TAG, "disposing task \"%s\"...", info.name);
            dttasker_dispose(tasker_handles[i]);

            tasker_handles[i] = NULL;
        }
    }

    // dispose the objects
    dtnetportal_dispose(netportal_handle);
    dtframer_dispose(framer_handle);
    dtiox_dispose(iox_handle);

    for (int i = 0; i < MAIN_MCP4728_CHANNEL_COUNT; i++)
    {
        dttimeseries_dispose(self->config.timeseries_handles[i]);
    }

    dtkvp_list_dispose(kvp_list);

    while (true)
    {
        dtruntime_sleep_milliseconds(1000);
    }
}

// --------------------------------------------------------------------------------------------
// handle incoming messages
static dterr_t*
_receive_callback(void* opaque_self, const char* topic, dtbuffer_t* buffer)
{
    dterr_t* dterr = NULL;
    main_t* self = (main_t*)opaque_self;
    dtkvp_list_t _kvp_list = { 0 }, *kvp_list = &_kvp_list;
    DTERR_ASSERT_NOT_NULL(self);

    dtlog_info(TAG, "netportal received message from client: \"%s\"", (const char*)buffer->payload);

    DTERR_C(dtkvp_list_init(kvp_list));
    DTERR_C(dtkvp_list_urldecode(kvp_list, (const char*)buffer->payload));
    const char* value_string;
    DTERR_C(dtkvp_list_get(kvp_list, "value", &value_string));
    if (value_string)
    {
        double value_double = 0.0;
        if (dtparse_double(value_string, &value_double))
        {
            int index = 1;
            dtlog_info(TAG, "setting MCP4728 [%d] voltage to %0.3f", index, value_double);
            dttimeseries_handle timeseries_handle = self->config.timeseries_handles[index];
            DTERR_C(dttimeseries_configure(timeseries_handle, kvp_list));
        }
    }

cleanup:
    // release memory from the received buffer
    dtbuffer_dispose(buffer);

    return dterr;
}
