#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <esp_modbus_common.h>
#include <esp_modbus_slave.h>

#include <dtcore/dterr.h>
#include <dtcore/dtringfifo.h>

#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtiox_modbus.h>

#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>

typedef struct dtiox_espidf_modbus_rtu_slave_stats_t
{
    int32_t future;
} dtiox_espidf_modbus_rtu_slave_stats_t;

typedef struct dtiox_espidf_modbus_rtu_slave_t
{
    DTIOX_COMMON_MEMBERS // int32_t model_number

      dtiox_espidf_modbus_rtu_slave_config_t cfg;

    dtiox_espidf_modbus_rtu_slave_stats_t stats;

    // libmodbus state
    void* mbc_slave_handler;
    bool _is_connected;

    dtsemaphore_handle rx_semaphore;

    // background thread control
    volatile bool stop_requested;
    int32_t poll_for_stop_millis;

    // to surface on next read()
    bool rx_overflow_pending;

    uint8_t* tx_fifo_storage;
    dtringfifo_t tx_fifo;
    uint8_t* rx_fifo_storage;
    dtringfifo_t rx_fifo;

    // RX thread plumbing
    dttasker_handle event_tasker_handle;

    // synchronization between threads
    dtlock_handle lock_handle;

    // holding registers arriving from the master will be buffered here until read() by the application
    uint16_t m2s_holding_regs[DTIOX_MODBUS_BLOB_TO_REGS(DTIOX_MODBUS_MAX_BLOB_BYTES)];

    // data to be sent to the master the next time it asks for DTIOX_MODBUS_CMD_GIVE_ME_ANY_DATA are stored here
    uint16_t s2m_status_holding_regs[2]; // [0] is status flags, [1] is length of data available for read in bytes

    // data to be sent to the master the next time it isues a read
    uint16_t s2m_blob_holding_regs[DTIOX_MODBUS_BLOB_TO_REGS(DTIOX_MODBUS_MAX_BLOB_BYTES)];

} dtiox_espidf_modbus_rtu_slave_t;

// task entry point
extern dterr_t*
dtiox_espidf_modbus_rtu_slave__event_task(void* self_, dttasker_handle tasker_handle);

extern dterr_t*
dtiox_espidf_modbus_rtu_slave__verify_rxtask_running(dtiox_espidf_modbus_rtu_slave_t* self, dterr_t* dterr_in);

// lock release helper
extern dterr_t*
dtiox_espidf_modbus_rtu_slave__release_lock(dtiox_espidf_modbus_rtu_slave_t* self, dterr_t* dterr_in);

// configuration converter
extern dterr_t*
dtiox_espidf_modbus_rtu_slave__fill_mb_communication_info( //
  uart_port_t uart_port,
  uint8_t slave_uid,
  const dtuart_helper_config_t* helper_uart_config,
  mb_communication_info_t* mb_communication_info);

dterr_t*
dtiox_espidf_modbus_rtu_slave__respond_to_put_blob( //
  dtiox_espidf_modbus_rtu_slave_t* self,
  int32_t register_offset,
  uint16_t* data_registers,
  int32_t data_length_in_bytes);

dterr_t*
dtiox_espidf_modbus_rtu_slave__respond_to_give_me_any_data( //
  dtiox_espidf_modbus_rtu_slave_t* self);
