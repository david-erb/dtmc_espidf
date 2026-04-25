#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtiox_modbus.h>
#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtmodbus_helpers.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtmc_espidf.h>

#include "dtiox_espidf_modbus_rtu_slave__private.h"

#define TAG "dtiox_espidf_modbus_rtu_slave"

// comment out the logging here
// #define dtlog_debug(TAG, ...)

dterr_t*
dtiox_espidf_modbus_rtu_slave__event_task(void* self_, dttasker_handle tasker_handle)
{
    dtiox_espidf_modbus_rtu_slave_t* self = (dtiox_espidf_modbus_rtu_slave_t*)self_;
    dterr_t* dterr = NULL;
    bool lock_acquired = false;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(tasker_handle);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);
    DTERR_ASSERT_NOT_NULL(self->mbc_slave_handler);

    dtlog_debug(TAG, "RTU slave RX task started");

    DTERR_C(dttasker_ready(tasker_handle));

    // mb_event_group_t filter_mb_event_group =              //
    //   MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD | //
    //   MB_EVENT_INPUT_REG_RD |                             //
    //   MB_EVENT_COILS_WR | MB_EVENT_COILS_RD |             //
    //   MB_EVENT_DISCRETE_RD |                              //
    //   MB_EVENT_STACK_STARTED | MB_EVENT_STACK_CONNECTED;

    mb_param_info_t mb_param_info = { 0 };

    while (true)
    {
        // mb_event_group_t got_mb_event_group;
        // got_mb_event_group = mbc_slave_check_event(self->mbc_slave_handler, filter_mb_event_group);
        // dtlog_debug(TAG, "mbc_slave_check_event returned: 0x%" PRIx32, (uint32_t)got_mb_event_group);

        esp_err_t err = mbc_slave_get_param_info(self->mbc_slave_handler, &mb_param_info, self->poll_for_stop_millis);
        if (err == ESP_ERR_TIMEOUT)
        {
            continue;
        }
        else if (err != ESP_OK)
        {
            dterr = dterr_new(
              DTERR_IO, DTERR_LOC, NULL, "mbc_slave_get_param_info failed " DTMC_ESPIDF_C_FORMAT, DTMC_ESPIDF_C_ARGS(err));
            goto cleanup;
        }

        bool stop_requested = self->stop_requested;
        if (stop_requested)
        {
            dtlog_debug(TAG, "RTU slave RX task stopping due to stop_requested");
            break;
        }

        // we never expect less than 2 registers because the first 2 registers are the command and length
        if (mb_param_info.size < 2)
        {
            dterr =
              dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "number of regs must be at least 2, got %" PRId32, mb_param_info.size);
            goto cleanup;
        }

        DTERR_C(dtlock_acquire(self->lock_handle));
        lock_acquired = true;

        // master is writing to us?
        if (mb_param_info.type == MB_EVENT_HOLDING_REG_WR)
        {
            // get the master's command from the first register of the write
            uint16_t cmd = ((uint16_t*)mb_param_info.address)[0];
            int32_t data_length_in_bytes = ((uint16_t*)mb_param_info.address)[1];
            int32_t data_length_in_regs = mb_param_info.size - 2; // total regs minus the 2 regs for cmd + length

            // allow odd byte counts by rounding up to the next register
            if ((data_length_in_bytes + 1) / 2 != data_length_in_regs)
            {
                dterr = dterr_new(DTERR_BADARG,
                  DTERR_LOC,
                  NULL,
                  "data length in bytes in cmd header (%" PRId32 ") does not match actual data length in bytes (%" PRId32 ")",
                  data_length_in_bytes,
                  data_length_in_regs * 2); // convert regs to bytes
                goto cleanup;
            }

            // master is writing a blob for us to consume?
            if (cmd == (uint16_t)DTIOX_MODBUS_CMD_PUT_BLOB)
            {
                uint16_t* data_registers = (uint16_t*)mb_param_info.address + 2; // skip cmd + len

                // we will absorb this blob into the RX FIFO for the application to read later
                DTERR_C(dtiox_espidf_modbus_rtu_slave__respond_to_put_blob( //
                  self,
                  mb_param_info.mb_offset,
                  data_registers,
                  data_length_in_bytes));
            }

            // master is writing asking us if we have any data to read?
            else if (cmd == (uint16_t)DTIOX_MODBUS_CMD_GIVE_ME_ANY_DATA)
            {
                // we will give back just the length of data queued
                DTERR_C(dtiox_espidf_modbus_rtu_slave__respond_to_give_me_any_data( //
                  self));
            }
            else
            {
                dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "unexpected cmd=0x%04" PRIx16 " in master write", cmd);
                goto cleanup;
            }
        }

        else
        {
            // dtlog_debug(TAG, "ignoring unhandled Modbus event type 0x%" PRIx32, (uint32_t)mb_param_info.type);
        }

        lock_acquired = false;
        DTERR_C(dtlock_release(self->lock_handle));
    }

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);

    dtlog_info(TAG, "RTU slave RX task exiting with%s error", dterr ? "" : "out");
    dtlog_dterr(TAG, dterr);

    return dterr;
}