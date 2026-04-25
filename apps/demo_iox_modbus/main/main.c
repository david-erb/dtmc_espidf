#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <mbcontroller.h>

// this concrete object is platform specific
#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>

#include <dtmc_base_demos/demo_iox.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtmc_base_demo_iox_t* demo = NULL;

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
        DTERR_C(dtiox_espidf_modbus_rtu_slave_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(dtmc_base_demo_iox_create(&demo));
        dtmc_base_demo_iox_config_t c = { 0 };
        c.iox_handle = iox_handle;
        c.node_name = "dtmc_espidf/uart1";
        DTERR_C(dtmc_base_demo_iox_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(dtmc_base_demo_iox_start(demo));

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the object
    dtiox_dispose(iox_handle);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
