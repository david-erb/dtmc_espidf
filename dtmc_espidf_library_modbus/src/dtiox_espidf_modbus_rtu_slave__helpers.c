#include <stdbool.h>
#include <stdint.h>

#include <esp_modbus_common.h>
#include <esp_modbus_slave.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtringfifo.h>

#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtmodbus_helpers.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>

#include "dtiox_espidf_modbus_rtu_slave__private.h"

#define TAG "dtiox_espidf_modbus_rtu_slave"

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave__release_lock(dtiox_espidf_modbus_rtu_slave_t* self, dterr_t* dterr_in)
{
    dterr_t* dterr = dtlock_release(self->lock_handle);
    // dtlog_debug(TAG, "%s: %p lock released", __func__, self->lock_handle);

    // let incoming error take precedence
    if (dterr_in != NULL)
    {
        dterr_dispose(dterr);
        return dterr_in;
    }
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave__verify_rxtask_running(dtiox_espidf_modbus_rtu_slave_t* self, dterr_t* dterr_in)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->event_tasker_handle);

    dttasker_info_t task_info = { 0 };
    DTERR_C(dttasker_get_info(self->event_tasker_handle, &task_info));

    if (task_info.status != RUNNING)
    {
        dterr = dterr_new(DTERR_STATE,
          DTERR_LOC,
          task_info.dterr,
          "rx task not running (status=%s)",
          dttasker_state_to_string(task_info.status));
        goto cleanup;
    }

cleanup:
    if (dterr_in != NULL)
    {
        if (dterr != NULL)
            dterr_append(dterr_in, dterr);
        dterr = dterr_in;
    }
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave__fill_mb_communication_info( //
  uart_port_t uart_port,
  uint8_t slave_uid,
  const dtuart_helper_config_t* helper_uart_config,
  mb_communication_info_t* mb_communication_info)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(helper_uart_config);
    DTERR_ASSERT_NOT_NULL(mb_communication_info);

    if (helper_uart_config->baudrate <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "baudrate must be > 0");
        goto cleanup;
    }

    mb_serial_opts_t* ser_opts = &mb_communication_info->ser_opts;
    ser_opts->mode = MB_RTU;
    ser_opts->port = uart_port;
    ser_opts->uid = slave_uid;

    // -------------------------------------------------------------------------
    // Baudrate

    ser_opts->baudrate = helper_uart_config->baudrate;

    // -------------------------------------------------------------------------
    // Parity

    switch (helper_uart_config->parity)
    {
        case DTUART_PARITY_NONE:
            ser_opts->parity = UART_PARITY_DISABLE;
            break;

        case DTUART_PARITY_EVEN:
            ser_opts->parity = UART_PARITY_EVEN;
            break;

        case DTUART_PARITY_ODD:
            ser_opts->parity = UART_PARITY_ODD;
            break;

        default:
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "invalid parity");
            goto cleanup;
    }

    // -------------------------------------------------------------------------
    // Data bits

    switch (helper_uart_config->data_bits)
    {
        case DTUART_DATA_BITS_7:
            ser_opts->data_bits = UART_DATA_7_BITS;
            break;

        case DTUART_DATA_BITS_8:
            ser_opts->data_bits = UART_DATA_8_BITS;
            break;

        default:
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "invalid data_bits");
            goto cleanup;
    }

    // -------------------------------------------------------------------------
    // Stop bits

    switch (helper_uart_config->stop_bits)
    {
        case DTUART_STOPBITS_1:
            ser_opts->stop_bits = UART_STOP_BITS_1;
            break;

        case DTUART_STOPBITS_2:
            ser_opts->stop_bits = UART_STOP_BITS_2;
            break;

        default:
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "invalid stop_bits");
            goto cleanup;
    }

    // -------------------------------------------------------------------------
    // Flow control (RTU typically does not use HW flow)

    if (helper_uart_config->flow)
    {
        if (helper_uart_config->flow != DTUART_FLOW_NONE)
        {
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "flow control not supported for Modbus RTU");
            goto cleanup;
        }
    }

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------
// lock before calling this, and release after
dterr_t*
dtiox_espidf_modbus_rtu_slave__respond_to_put_blob( //
  dtiox_espidf_modbus_rtu_slave_t* self,
  int32_t register_offset,
  uint16_t* data_registers,
  int32_t data_length_in_bytes)
{
    dterr_t* dterr = NULL;

    // blob writes always start at this offset
    if (register_offset != DTIOX_MODBUS_REG_M2S_CMD)
    {
        dterr = dterr_new(DTERR_BADARG,
          DTERR_LOC,
          NULL,
          "unexpected register offset %" PRId32 " for master put blob command, expected %d",
          register_offset,
          DTIOX_MODBUS_REG_M2S_CMD);
        goto cleanup;
    }

    if (data_length_in_bytes <= 0)
    {
        dterr = dterr_new(DTERR_BADARG,
          DTERR_LOC,
          NULL,
          "expected positive data_length_in_bytes in master write, got %" PRId32,
          data_length_in_bytes);
        goto cleanup;
    }

    if (data_length_in_bytes > (uint16_t)self->cfg.max_blob_bytes)
    {
        dterr = dterr_new(DTERR_BADARG,
          DTERR_LOC,
          NULL,
          "master write data_length_in_bytes=%" PRIu16 " exceeds configured max_blob_bytes=%" PRId32,
          data_length_in_bytes,
          self->cfg.max_blob_bytes);
        goto cleanup;
    }

    // stage registers to bytes
    // TODO: Avoid double copy in dtiox_espidf_modbus_rtu_slave__respond_to_put_blob().
    uint8_t data_bytes[DTIOX_MODBUS_MAX_BLOB_BYTES];
    dtmodbus_helpers_unpack_regs_to_bytes(data_registers, data_length_in_bytes, data_bytes);

    // copy bytes into the FIFO
    int32_t pushed_bytes = dtringfifo_push(&self->rx_fifo, data_bytes, data_length_in_bytes);

    if (pushed_bytes < data_length_in_bytes)
        self->rx_overflow_pending = true;

    // trigger semaphore so that pending read() will return with data (even if some got dropped)
    if (self->rx_semaphore != NULL && pushed_bytes > 0)
        DTERR_C(dtsemaphore_post(self->rx_semaphore));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// master is signalling intent to read blob data, so prepare the S2M shadow registers
// lock before calling this, and release after
dterr_t*
dtiox_espidf_modbus_rtu_slave__respond_to_give_me_any_data( //
  dtiox_espidf_modbus_rtu_slave_t* self)
{
    dterr_t* dterr = NULL;

    // snapshot the current blob data into the S2M blob holding registers which will supply the next read from master
    int32_t available_bytes = dtringfifo_pop( //
      &self->tx_fifo,
      (uint8_t*)(self->s2m_blob_holding_regs),
      (int32_t)sizeof(self->s2m_blob_holding_regs)); // total size minus the 4 bytes for the 2 status registers

    // pack back into the same place
    dtmodbus_helpers_pack_bytes_to_regs( //
      (uint8_t*)self->s2m_blob_holding_regs,
      available_bytes,
      self->s2m_blob_holding_regs);

    // set status register to indicate whether we have any data available for master to read
    self->s2m_status_holding_regs[0] = (int32_t)(available_bytes > 0);

    // snapshot the length into the S2M status holding registers so the master knows how much to read
    self->s2m_status_holding_regs[1] = (uint16_t)available_bytes;

    return dterr;
}
