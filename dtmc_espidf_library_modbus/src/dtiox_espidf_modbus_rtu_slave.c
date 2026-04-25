// dtiox_espidf_modbus_rtu_slave.c
// in 2.x it's a "managed component" and they started with a new "stable" API:
// https://docs.espressif.com/projects/esp-modbus/en/stable/esp32/port_initialization.html#modbus-api-port-initialization
//      mbc_slave_create_serial()
// NOT in the "latest" API:
// https://docs.espressif.com/projects/esp-modbus/en/latest/esp32/port_initialization.html#modbus-api-port-initialization
//      mbc_slave_init()

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_modbus_common.h>
#include <esp_modbus_slave.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtringfifo.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc_base/dtmodbus_helpers.h>
#include <dtmc_base/dtuart_helpers.h>

#include <dtmc/dtiox_espidf_modbus_rtu_slave.h>
#include <dtmc/dtmc_espidf.h>

#include "dtiox_espidf_modbus_rtu_slave__private.h"

#define TAG "dtiox_espidf_modbus_rtu_slave"
// #define dtlog_debug(TAG, ...)

// vtable
DTIOX_INIT_VTABLE(dtiox_espidf_modbus_rtu_slave);
DTOBJECT_INIT_VTABLE(dtiox_espidf_modbus_rtu_slave);

// -----------------------------------------------------------------------------
// Creation / initialization

dterr_t*
dtiox_espidf_modbus_rtu_slave_create(dtiox_espidf_modbus_rtu_slave_t** self_ptr)
{
    dterr_t* dterr = NULL;
    dtiox_espidf_modbus_rtu_slave_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtiox_espidf_modbus_rtu_slave_t), "dtiox_espidf_modbus_rtu_slave_t", (void**)&self));

    *self_ptr = self;
    DTERR_C(dtiox_espidf_modbus_rtu_slave_init(self));

cleanup:
    if (dterr)
    {
        dtheaper_free(self);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_espidf_modbus_rtu_slave_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave_init(dtiox_espidf_modbus_rtu_slave_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ESPIDF_MODBUS_SLAVE;

    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_espidf_modbus_rtu_slave_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtiox_espidf_modbus_rtu_slave_object_vt));

    DTERR_C(dtringfifo_init(&self->tx_fifo));
    DTERR_C(dtringfifo_init(&self->rx_fifo));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave_configure(dtiox_espidf_modbus_rtu_slave_t* self,
  const dtiox_espidf_modbus_rtu_slave_config_t* cfg)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(cfg);

    DTERR_C(dtuart_helper_validate(&cfg->uart_config));

    self->cfg = *cfg;

    if (self->cfg.slave_id == 0)
        self->cfg.slave_id = 1;

    if (self->cfg.max_blob_bytes <= 0 || self->cfg.max_blob_bytes > DTIOX_MODBUS_MAX_BLOB_BYTES)
        self->cfg.max_blob_bytes = DTIOX_MODBUS_MAX_BLOB_BYTES;

    {
        if (cfg->tx_ring_capacity < 0)
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "tx_ring_capacity must be >= 0 (0 means default, got %" PRId32 ")",
              cfg->tx_ring_capacity);
            goto cleanup;
        }

        if (self->cfg.tx_ring_capacity == 0)
            self->cfg.tx_ring_capacity = 1024;

        DTERR_C(dtheaper_alloc(
          self->cfg.tx_ring_capacity, "dtiox_espidf_modbus_rtu_slave tx_fifo_storage", (void**)&self->tx_fifo_storage));

        dtringfifo_config_t fifo_cfg = {
            .buffer = self->tx_fifo_storage,
            .capacity = self->cfg.tx_ring_capacity,
        };

        DTERR_C(dtringfifo_configure(&self->tx_fifo, &fifo_cfg));
    }

    {
        if (cfg->rx_ring_capacity < 0)
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "rx_ring_capacity must be >= 0 (0 means default, got %" PRId32 ")",
              cfg->rx_ring_capacity);
            goto cleanup;
        }

        if (self->cfg.rx_ring_capacity == 0)
            self->cfg.rx_ring_capacity = 1024;

        DTERR_C(dtheaper_alloc(
          self->cfg.rx_ring_capacity, "dtiox_espidf_modbus_rtu_slave rx_fifo_storage", (void**)&self->rx_fifo_storage));

        dtringfifo_config_t fifo_cfg = {
            .buffer = self->rx_fifo_storage,
            .capacity = self->cfg.rx_ring_capacity,
        };

        DTERR_C(dtringfifo_configure(&self->rx_fifo, &fifo_cfg));
    }

    self->poll_for_stop_millis = 50;

    if (self->cfg.event_task_priority == 0)
        self->cfg.event_task_priority = DTTASKER_PRIORITY_NORMAL_HIGHEST;

    {
        dttasker_config_t c = { 0 };
        c.name = "dtiox_modbus";
        c.tasker_entry_point_fn = dtiox_espidf_modbus_rtu_slave__event_task;
        c.tasker_entry_point_arg = self;
        c.priority = self->cfg.event_task_priority;
        c.stack_size = 4096;
        DTERR_C(dttasker_create(&self->event_tasker_handle, &c));
    }

    DTERR_C(dtlock_create(&self->lock_handle));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Attach / detach

dterr_t*
dtiox_espidf_modbus_rtu_slave_attach(dtiox_espidf_modbus_rtu_slave_t* self)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);

    // Set UART log level
    // esp_log_level_set("uart", ESP_LOG_DEBUG);
    // esp_log_level_set("mb_port.serial", ESP_LOG_DEBUG);
    // esp_log_level_set("mbc_serial.slave", ESP_LOG_DEBUG);
    // esp_log_level_set("mb_object.slave", ESP_LOG_DEBUG);
    // esp_log_level_set("vfs_calls", ESP_LOG_DEBUG);

    dtlog_debug(TAG, "attaching");

    mb_communication_info_t mb_communication_info = { 0 };
    DTERR_C(dtiox_espidf_modbus_rtu_slave__fill_mb_communication_info( //
      self->cfg.uart_port_num,
      (uint8_t)self->cfg.slave_id,
      &self->cfg.uart_config,
      &mb_communication_info));

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;

    DTMC_ESPIDF_C(mbc_slave_create_serial(&mb_communication_info, &self->mbc_slave_handler));

    {
        // configure holding register area for incoming writes from master
        mb_register_area_descriptor_t area_descriptor = {
            .type = MB_PARAM_HOLDING,
            .start_offset = DTIOX_MODBUS_REG_M2S_CMD,
            .address = self->m2s_holding_regs,
            .size = sizeof(self->m2s_holding_regs), // bytes, not registers
        };
        DTMC_ESPIDF_C(mbc_slave_set_descriptor(self->mbc_slave_handler, area_descriptor));
    }

    {
        // configure holding register area for status to be read by master
        mb_register_area_descriptor_t area_descriptor = {
            .type = MB_PARAM_HOLDING,
            .start_offset = DTIOX_MODBUS_REG_S2M_STATUS,
            .address = self->s2m_status_holding_regs,
            .size = sizeof(self->s2m_status_holding_regs), // bytes, not registers
        };
        DTMC_ESPIDF_C(mbc_slave_set_descriptor(self->mbc_slave_handler, area_descriptor));
    }

    {
        // configure holding register area for blob data to be read by master
        mb_register_area_descriptor_t area_descriptor = {
            .type = MB_PARAM_HOLDING,
            .start_offset = DTIOX_MODBUS_REG_S2M_DATA,
            .address = self->s2m_blob_holding_regs,
            .size = sizeof(self->s2m_blob_holding_regs), // bytes, not registers
        };
        DTMC_ESPIDF_C(mbc_slave_set_descriptor(self->mbc_slave_handler, area_descriptor));
    }

    {
        int32_t tx = self->cfg.tx_pin;
        int32_t rx = self->cfg.rx_pin;
        int32_t rts = self->cfg.rts_pin;
        int32_t cts = self->cfg.cts_pin;

        if (tx < 0)
            tx = UART_PIN_NO_CHANGE;
        if (rx < 0)
            rx = UART_PIN_NO_CHANGE;
        if (rts < 0)
            rts = UART_PIN_NO_CHANGE;
        if (cts < 0)
            cts = UART_PIN_NO_CHANGE;

        DTMC_ESPIDF_C(uart_set_pin(self->cfg.uart_port_num, tx, rx, rts, cts));

        // Set UART driver mode to Half Duplex
        DTMC_ESPIDF_C(uart_set_mode(self->cfg.uart_port_num, UART_MODE_RS485_HALF_DUPLEX));
    }

    DTMC_ESPIDF_C(mbc_slave_start(self->mbc_slave_handler));

    self->rx_overflow_pending = false;
    self->stop_requested = false;

    // Start RX task
    DTERR_C(dttasker_start(self->event_tasker_handle));

cleanup:
    if (dterr != NULL)
    {
        mbc_slave_delete(self->mbc_slave_handler);
        self->mbc_slave_handler = NULL;
    }

    if (lock_acquired)
        dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);

    if (dterr == NULL)
        dtlog_debug(TAG, "attached successfully");

    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_modbus_rtu_slave_detach(dtiox_espidf_modbus_rtu_slave_t* self)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);

    dtlog_debug(TAG, "detaching");

    // you have to stop the RX task before messing with self->mbc_slave_handler
    // right now, this is best-effort only
    self->stop_requested = true;
    // wait a bit longer than the RX task's poll timeout to increase chances that the RX task has stopped
    // this should be a spin wait checking a flag that the RX task sets when it exits, but this is good enough for now
    dtruntime_sleep_milliseconds(self->poll_for_stop_millis * 2);

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;

    if (self->mbc_slave_handler != NULL)
    {
        DTMC_ESPIDF_C(mbc_slave_delete(self->mbc_slave_handler));
        self->mbc_slave_handler = NULL;
    }

    dtringfifo_reset(&self->tx_fifo);
    dtringfifo_reset(&self->rx_fifo);
    self->rx_overflow_pending = false;

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);

    if (dterr == NULL)
        dtlog_debug(TAG, "detached successfully");

    return dterr;
}

// -----------------------------------------------------------------------------
// Enable

dterr_t*
dtiox_espidf_modbus_rtu_slave_enable(dtiox_espidf_modbus_rtu_slave_t* self, bool enabled)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtlock_acquire(self->lock_handle));

    // Clear buffers + flags for predictable state.
    dtringfifo_reset(&self->rx_fifo);
    self->rx_overflow_pending = false;

    DTERR_C(dtlock_release(self->lock_handle));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Write (PUT_BLOB), best effort, absorb as much as possible with no waiting

dterr_t*
dtiox_espidf_modbus_rtu_slave_write( //
  dtiox_espidf_modbus_rtu_slave_t* self,
  const uint8_t* buf,
  int32_t len,
  int32_t* out_written)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);
    DTERR_ASSERT_NOT_NULL(self->mbc_slave_handler);

    if (len < 0)
        return dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "len < 0");

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;

    // we just stuff the bytes into the fifo which we will feed the master when he asks
    *out_written = dtringfifo_push(&self->tx_fifo, buf, len);

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);

    return dterr;
}

// -----------------------------------------------------------------------------
// Read (non-blocking from RX FIFO)

dterr_t*
dtiox_espidf_modbus_rtu_slave_read( //
  dtiox_espidf_modbus_rtu_slave_t* self,
  uint8_t* buf,
  int32_t buf_len,
  int32_t* out_read)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);
    DTERR_ASSERT_NOT_NULL(self->mbc_slave_handler);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);

    if (buf_len < 0)
        return dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "buf_len < 0");

    // surface overflow from rx task
    if (self->rx_overflow_pending)
    {
        self->rx_overflow_pending = false;
        dterr = dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "rx fifo overflow");
        goto cleanup;
    }

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;

    *out_read = dtringfifo_pop(&self->rx_fifo, buf, buf_len);

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);

    dterr = dtiox_espidf_modbus_rtu_slave__verify_rxtask_running(self, dterr);

    return dterr;
}

// -----------------------------------------------------------------------------
// set_rx_semaphore

dterr_t*
dtiox_espidf_modbus_rtu_slave_set_rx_semaphore(dtiox_espidf_modbus_rtu_slave_t* self, dtsemaphore_handle rx_semaphore)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;
    self->rx_semaphore = rx_semaphore;
    DTERR_C(dtlock_release(self->lock_handle));
    lock_acquired = false;

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);
    return dterr;
}

// -----------------------------------------------------------------------------
// concat_format

dterr_t*
dtiox_espidf_modbus_rtu_slave_concat_format(dtiox_espidf_modbus_rtu_slave_t* self,
  char* in_str,
  char* separator,
  char** out_str)
{
    dterr_t* dterr = NULL;
    bool lock_acquired = false;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    *out_str = in_str;
    const char* sep = (separator != NULL) ? separator : "";

    DTERR_C(dtlock_acquire(self->lock_handle));
    lock_acquired = true;

    int32_t tx_capacity = self->cfg.tx_ring_capacity;
    int32_t rx_capacity = self->cfg.rx_ring_capacity;

    char object_string[256];
    dtiox_espidf_modbus_rtu_slave_to_string(self, object_string, sizeof(object_string));

    lock_acquired = false;
    DTERR_C(dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr));

    *out_str = dtstr_concat_format(
      *out_str, sep, "%s tx_ring_capacity=%" PRId32 " rx_ring_capacity=%" PRId32, object_string, tx_capacity, rx_capacity);

cleanup:
    if (lock_acquired)
        dterr = dtiox_espidf_modbus_rtu_slave__release_lock(self, dterr);
    return dterr;
}

// -----------------------------------------------------------------------------
// dispose

void
dtiox_espidf_modbus_rtu_slave_dispose(dtiox_espidf_modbus_rtu_slave_t* self)
{
    if (self == NULL)
        return;

    // TODO: Make dtiox_espidf_modbus_rtu_slave_dispose() stop the task and wait for the task to exit.

    dtlock_dispose(self->lock_handle);

    if (self->mbc_slave_handler != NULL)
    {
        mbc_slave_delete(self->mbc_slave_handler);
        self->mbc_slave_handler = NULL;
    }

    dtheaper_free(self->rx_fifo_storage);
    self->rx_fifo_storage = NULL;
    memset(&self->rx_fifo, 0, sizeof(self->rx_fifo));

    dtheaper_free(self->tx_fifo_storage);
    self->tx_fifo_storage = NULL;
    memset(&self->tx_fifo, 0, sizeof(self->tx_fifo));

    dtheaper_free(self);
}
