#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtnetportal.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtframer_simple.h>
#include <dtmc_base/dtnetportal_iox.h>

// these concrete objects are platform specific
#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>

#include <dtmc_base_benchmarks/benchmark_netportal_simplex.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtframer_handle framer_handle = NULL;
    dttasker_handle rx_tasker_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;

    benchmark_t* benchmark = NULL;

    // ==== print the currently registered devices ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));

        dtlog_info(TAG, "devices:\n%s", s);

        dtstr_dispose(s);
    }

    // === create the concrete IOX object we need ===
    {
        dtiox_espidf_modbus_rtu_slave_t* o = NULL;
        DTERR_C(dtiox_espidf_modbus_rtu_slave_create(&o));
        iox_handle = (dtiox_handle)o;
        dtiox_espidf_modbus_rtu_slave_config_t c = { 0 };
        c.uart_port_num = 1;
        c.slave_id = 13;
        c.tx_pin = 17;
        c.rx_pin = 16;
        c.rts_pin = -1;
        c.cts_pin = -1;
        c.uart_config.baudrate = 115200;
        c.uart_config.data_bits = DTUART_DATA_BITS_8;
        c.uart_config.parity = DTUART_PARITY_NONE;
        c.uart_config.stop_bits = DTUART_STOPBITS_2;
        c.uart_config.flow = DTUART_FLOW_NONE;
        c.event_task_priority = DTTASKER_PRIORITY_NORMAL_HIGHEST;
        DTERR_C(dtiox_espidf_modbus_rtu_slave_configure(o, &c));
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
        c.rx_tasker_priority = DTTASKER_PRIORITY_NORMAL_HIGH;
        DTERR_C(dtnetportal_iox_configure(o, &c));
    }

    // === create and configure the benchmark instance ===
    {
        DTERR_C(benchmark_create(&benchmark));
        benchmark_config_t c = { 0 };
        c.netportal_handle = netportal_handle;
        c.is_server = true; // modbus RTU slave always acts as server
        c.app_core = 1;     // run the busywork on a different core than the netportal and framer tasks
        DTERR_C(benchmark_configure(benchmark, &c));
    }

    // === start the benchmark ===
    DTERR_C(benchmark_start(benchmark));

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the benchmark instance
    benchmark_dispose(benchmark);

    // dispose the objects
    dtnetportal_dispose(netportal_handle);
    dttasker_dispose(rx_tasker_handle);
    dtframer_dispose(framer_handle);
    dtiox_dispose(iox_handle);

    dtlog_info(TAG, "%s finished", benchmark_name);

    // Wait indefinitely to prevent the program from exiting.
    while (true)
    {
        dtruntime_sleep_milliseconds(1000);
    }
}
