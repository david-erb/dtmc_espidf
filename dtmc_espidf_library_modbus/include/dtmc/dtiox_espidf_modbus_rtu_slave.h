#pragma once

#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dtuart_helpers.h>

// forward-declare concrete type
typedef struct dtiox_espidf_modbus_rtu_slave_t dtiox_espidf_modbus_rtu_slave_t;

typedef struct dtiox_espidf_modbus_rtu_slave_config_t
{
    // ESP-IDF UART port number, e.g., 0, 1, 2 (cast to uart_port_t in .c)
    int32_t uart_port_num;

    dtuart_helper_config_t uart_config;

    // Modbus RTU slave id (unit id)
    int32_t slave_id; // 1..247 ; 0 => default 1

    // GPIO pin numbers (ESP-IDF-style, e.g., 1..39); use -1 to leave unchanged
    int32_t tx_pin;
    int32_t rx_pin;
    int32_t rts_pin; // -1 for unused
    int32_t cts_pin; // -1 for unused

    // Max blob size accepted for write/read buffering (clamped to DTIOX_MODBUS_MAX_BLOB_BYTES).
    int32_t max_blob_bytes; // 0 => DTIOX_MODBUS_MAX_BLOB_BYTES

    // Internal TX FIFO capacity in bytes (not wire). Must be >= max_blob_bytes.
    int32_t tx_ring_capacity; // 0 => default (e.g. 1024)

    // Internal RX FIFO capacity in bytes (not wire). Must be >= max_blob_bytes.
    int32_t rx_ring_capacity; // 0 => default (e.g. 1024)

    // If true, try to configure libmodbus for RS485 mode (where supported).
    // This is helpful when using USB-RS485 adapters or UARTs with RTS-based DE control.
    bool rs485_mode; // default true

    // If true, enable libmodbus RTS toggling (where supported) for RS485 direction control.
    // If your hardware auto-controls DE/RE, set false.
    bool rts_toggle; // default true

    // Task priority for the internal RX task that polls the UART and feeds the Modbus stack.
    // want this above netportal and framer tasks to minimize latency
    // but lower than espmodbus (see CONFIG_FMB_PORT_TASK_PRIO in sdkconfig)
    dttasker_priority_t event_task_priority; // default DTTASKER_PRIORITY_NORMAL_HIGHEST

} dtiox_espidf_modbus_rtu_slave_config_t;

extern dterr_t*
dtiox_espidf_modbus_rtu_slave_create(dtiox_espidf_modbus_rtu_slave_t** self_ptr);

extern dterr_t*
dtiox_espidf_modbus_rtu_slave_init(dtiox_espidf_modbus_rtu_slave_t* self);

extern dterr_t*
dtiox_espidf_modbus_rtu_slave_configure(dtiox_espidf_modbus_rtu_slave_t* self,
  const dtiox_espidf_modbus_rtu_slave_config_t* cfg);

// -----------------------------------------------------------------------------
// Interface plumbing.

DTIOX_DECLARE_API(dtiox_espidf_modbus_rtu_slave);
DTOBJECT_DECLARE_API(dtiox_espidf_modbus_rtu_slave);
