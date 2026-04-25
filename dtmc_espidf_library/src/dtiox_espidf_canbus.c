// ESP-IDF backend for dtiox — CAN (TWAI) on ESP32 (new on-chip driver)

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>

#include <esp_twai.h>
#include <esp_twai_onchip.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtringfifo.h>
#include <dtcore/dtstr.h>

#include <dtmc/dtmc.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtiox_espidf_canbus.h>

#define TAG "dtiox_espidf_canbus"

#define dtiox_espidf_canbus_DEFAULT_RX_RING_CAPACITY 1024
#define dtiox_espidf_canbus_DEFAULT_TX_QUEUE_LEN 16

// vtable
DTIOX_INIT_VTABLE(dtiox_espidf_canbus);
DTOBJECT_INIT_VTABLE(dtiox_espidf_canbus);

// Concrete type
typedef struct dtiox_espidf_canbus_t
{
    DTIOX_COMMON_MEMBERS
    bool _is_malloced;

    dtiox_espidf_canbus_config_t config;

    // New TWAI node state
    twai_node_handle_t node;
    bool node_created;
    bool node_enabled;

    // RX FIFO: ISR producer, foreground consumer
    dtringfifo_t rx_fifo;
    uint8_t* rx_fifo_storage;

    // Optional semaphore to wake user code (not used from ISR here)
    dtsemaphore_handle rx_semaphore;

    // Control whether incoming bytes are pushed into FIFO
    bool rx_enabled;

    // Overflow handling: set when FIFO write fails; reported on next read.
    bool rx_overflow_flag;
    int32_t fifo_dropped_bytes;

    // RX/TX counters (frames + bytes), clamped to INT32_MAX
    int32_t rx_frames;
    int32_t rx_bytes;
    int32_t tx_frames;
    int32_t tx_bytes;

    // Event/alert counters, clamped to INT32_MAX
    int32_t alert_rx_done;
    int32_t alert_tx_done;
    int32_t alert_error;

    // latest state change info
    twai_error_state_t old_state;
    twai_error_state_t new_state;

    // latest error
    twai_error_flags_t last_error_flags;

} dtiox_espidf_canbus_t;

// -----------------------------------------------------------------------------
// Internal helpers

static inline int32_t
_rx_fifo_capacity_from_config(const dtiox_espidf_canbus_t* self)
{
    int32_t capacity = self->config.rx_ring_capacity;
    if (capacity <= 0)
        capacity = dtiox_espidf_canbus_DEFAULT_RX_RING_CAPACITY;
    return capacity;
}

// -----------------------------------------------------------------------------
// Simple saturating add for int32_t counters
static inline void
_counter_add(int32_t* counter, int32_t delta)
{
    if (!counter || delta <= 0)
        return;

    if (*counter >= INT32_MAX)
    {
        *counter = INT32_MAX;
        return;
    }

    if (INT32_MAX - *counter >= delta)
        *counter += delta;
    else
        *counter = INT32_MAX;
}

// -----------------------------------------------------------------------------
// Push bytes into FIFO (ISR producer)
// NOTE: This assumes dtringfifo_push is safe for ISR/task producer-consumer
// in your configuration. If not, wrap with an ISR-safe primitive.
static inline int32_t
_rx_fifo_push(dtiox_espidf_canbus_t* self, const uint8_t* src, int32_t len)
{
    if (!self || !src || len <= 0)
        return 0;

    int32_t written = dtringfifo_push(&self->rx_fifo, src, len);
    int32_t dropped = len - written;
    if (dropped > 0)
    {
        self->rx_overflow_flag = true;
        if (INT32_MAX - self->fifo_dropped_bytes >= dropped)
            self->fifo_dropped_bytes += dropped;
        else
            self->fifo_dropped_bytes = INT32_MAX;
    }
    return written;
}

// -----------------------------------------------------------------------------
// Pop bytes from FIFO (foreground consumer)
static inline int32_t
_rx_fifo_pop(dtiox_espidf_canbus_t* self, uint8_t* dest, int32_t len)
{
    if (!self || !dest || len <= 0)
        return 0;

    return dtringfifo_pop(&self->rx_fifo, dest, len);
}

// -----------------------------------------------------------------------------
// TWAI ISR callbacks (new on-chip driver)

static bool IRAM_ATTR
_twai_on_rx_done(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx)
{
    (void)edata;
    dtiox_espidf_canbus_t* self = (dtiox_espidf_canbus_t*)user_ctx;

    _counter_add(&self->alert_rx_done, 1);

    uint8_t data[8] = { 0 };
    twai_frame_t rx_frame = {
        .buffer = data,
        .buffer_len = sizeof(data),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK)
    {
        int32_t len = (int32_t)rx_frame.buffer_len;
        if (len < 0)
            len = 0;
        if (len > 8)
            len = 8;

        _counter_add(&self->rx_frames, 1);
        _counter_add(&self->rx_bytes, len);

        if (self->rx_enabled && len > 0)
        {
            (void)_rx_fifo_push(self, data, len);
            // If you later add an ISR-safe dtsemaphore_post_from_isr(),
            // you can signal self->rx_semaphore here.
        }
    }

    // We didn't explicitly wake a higher-priority task here.
    return false;
}

// -----------------------------------------------------------------------------
static bool IRAM_ATTR
_twai_on_tx_done(twai_node_handle_t handle, const twai_tx_done_event_data_t* edata, void* user_ctx)
{
    (void)handle;
    dtiox_espidf_canbus_t* self = (dtiox_espidf_canbus_t*)user_ctx;

    _counter_add(&self->alert_tx_done, 1);

    if (edata && edata->done_tx_frame)
    {
        int32_t len = (int32_t)edata->done_tx_frame->buffer_len;
        if (len < 0)
            len = 0;
        if (len > 8)
            len = 8;
        // Count successful TX bytes again here if you want callback-level stats.
        // We already count in dtiox_espidf_canbus_write; this is just alert stats.
    }

    return false;
}

// -----------------------------------------------------------------------------
static bool IRAM_ATTR
_twai_on_error(twai_node_handle_t handle, const twai_error_event_data_t* edata, void* user_ctx)
{
    (void)handle;
    dtiox_espidf_canbus_t* self = (dtiox_espidf_canbus_t*)user_ctx;

    _counter_add(&self->alert_error, 1);

    self->last_error_flags = edata->err_flags;

    return false;
}

// -----------------------------------------------------------------------------
static bool IRAM_ATTR
_twai_on_state_change(twai_node_handle_t handle, const twai_state_change_event_data_t* edata, void* user_ctx)
{
    (void)handle;
    dtiox_espidf_canbus_t* self = (dtiox_espidf_canbus_t*)user_ctx;

    self->old_state = edata->old_sta;
    self->new_state = edata->new_sta;

    return false;
}

// -----------------------------------------------------------------------------
// TWAI error state to string
static const char*
_dtiox_espidf_canbus_twai_error_state_to_str(twai_error_state_t state)
{
    switch (state)
    {
        case TWAI_ERROR_ACTIVE:
            return "ACTIVE";
        case TWAI_ERROR_WARNING:
            return "WARNING";
        case TWAI_ERROR_PASSIVE:
            return "PASSIVE";
        case TWAI_ERROR_BUS_OFF:
            return "BUS_OFF";
        default:
            return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// TWAI error state to string
static const char*
_dtiox_espidf_canbus_twai_error_flags_to_str(twai_error_flags_t flags, char* buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0)
        return "INVALID_BUFFER";

    buffer[0] = '\0';

    if (flags.arb_lost)
    {
        strncat(buffer, "|ARB_LOST", buffer_len);
    }
    if (flags.bit_err)
    {
        strncat(buffer, "|BIT_ERR", buffer_len);
    }
    if (flags.form_err)
    {
        strncat(buffer, "|FORM_ERR", buffer_len);
    }
    if (flags.stuff_err)
    {
        strncat(buffer, "|STUFF_ERR", buffer_len);
    }
    if (flags.ack_err)
    {
        strncat(buffer, "|ACK_ERR", buffer_len);
    }

    if (!buffer[0])
    {
        strncat(buffer, "|NONE", buffer_len);
    }

    strncat(buffer, "|", buffer_len);

    buffer[buffer_len - 1] = '\0';

    return buffer;
}

// -----------------------------------------------------------------------------
// check that error state is valid to continue read or write operations
static dterr_t*
_dtiox_espidf_canbus_check_error_state(dtiox_espidf_canbus_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (self->new_state != TWAI_ERROR_ACTIVE && self->new_state != (twai_error_state_t)-1)
    {
        char buffer[64] = { 0 };
        _dtiox_espidf_canbus_twai_error_flags_to_str(self->last_error_flags, buffer, sizeof(buffer));

        dterr = dterr_new(DTERR_FAIL,
          DTERR_LOC,
          NULL,
          "CAN node in error state (old=%s new=%s) flags=%s",
          _dtiox_espidf_canbus_twai_error_state_to_str(self->old_state),
          _dtiox_espidf_canbus_twai_error_state_to_str(self->new_state),
          buffer);
        goto cleanup;
    }
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Public helpers

dterr_t*
dtiox_espidf_canbus_create(dtiox_espidf_canbus_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtiox_espidf_canbus_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtiox_espidf_canbus_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_espidf_canbus_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_canbus_init(dtiox_espidf_canbus_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ESPIDF_CAN;

    DTERR_C(dtringfifo_init(&self->rx_fifo));

    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_espidf_canbus_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtiox_espidf_canbus_object_vt));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_espidf_canbus_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------
// Configure: we still accept cfg, but hard-code the low-level node settings.
// cfg is mainly used for the CAN identifier / extended-ID flag.

dterr_t*
dtiox_espidf_canbus_configure(dtiox_espidf_canbus_t* self, const dtiox_espidf_canbus_config_t* cfg)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(cfg);

    // Keep a copy in case higher layers care (e.g., tx_identifier, use_extended_id).
    self->config = *cfg;

    // NOTE: tx/rx GPIO, bitrate, mode, loopback, etc are *ignored* here.
    // They are fixed in dtiox_espidf_canbus_attach() to:
    //   TX = GPIO 4, RX = GPIO 5, bitrate = 250k, normal mode, no loopback.

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_canbus_attach(dtiox_espidf_canbus_t* self DTIOX_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    // Allocate and configure RX FIFO storage
    {
        int32_t capacity = _rx_fifo_capacity_from_config(self);
        if (capacity <= 0)
        {
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "rx_ring_capacity must be > 0");
            goto cleanup;
        }

        if (self->rx_fifo_storage)
        {
            free(self->rx_fifo_storage);
            self->rx_fifo_storage = NULL;
        }

        self->rx_fifo_storage = (uint8_t*)malloc((size_t)capacity);
        if (!self->rx_fifo_storage)
        {
            dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc rx ring buffer %d bytes failed", capacity);
            goto cleanup;
        }

        dtringfifo_config_t fifo_cfg = {
            .buffer = self->rx_fifo_storage,
            .capacity = capacity,
        };

        DTERR_C(dtringfifo_configure(&self->rx_fifo, &fifo_cfg));
        self->rx_overflow_flag = false;
        self->fifo_dropped_bytes = 0;
    }

    // Create TWAI node: *hard-coded* GPIO and bitrate
    {
        twai_onchip_node_config_t node_config = {
            .io_cfg.tx = GPIO_NUM_4,      // TWAI TX GPIO pin
            .io_cfg.rx = GPIO_NUM_5,      // TWAI RX GPIO pin
            .bit_timing.bitrate = 500000, // 500 kbit/s (hard-coded)
            .tx_queue_depth = dtiox_espidf_canbus_DEFAULT_TX_QUEUE_LEN,
            .flags.enable_loopback = false, /**< The TWAI controller receive back frames what it send out */
            .flags.enable_self_test = false /**< Transmission does not require acknowledgment. Use this mode for self testing */
        };

        DTMC_ESPIDF_C(twai_new_node_onchip(&node_config, &self->node));
        self->node_created = true;
    }

    // Register all event callbacks, using ISR for RX
    {
        twai_event_callbacks_t callbacks = {
            .on_rx_done = _twai_on_rx_done,
            .on_tx_done = _twai_on_tx_done,
            .on_error = _twai_on_error,
            .on_state_change = _twai_on_state_change,
        };

        DTMC_ESPIDF_C(twai_node_register_event_callbacks(self->node, &callbacks, self));
    }

    // Default: node is not enabled yet; enable() will call twai_node_enable().
    self->rx_enabled = false;

    // Reset counters
    self->rx_frames = 0;
    self->rx_bytes = 0;
    self->tx_frames = 0;
    self->tx_bytes = 0;
    self->alert_rx_done = 0;
    self->alert_tx_done = 0;
    self->alert_error = 0;
    self->old_state = (twai_error_state_t)(-1);
    self->new_state = (twai_error_state_t)(-1);

cleanup:
    if (dterr)
    {
        if (self->node_created)
        {
            if (self->node_enabled)
            {
                (void)twai_node_disable(self->node);
                self->node_enabled = false;
            }
            (void)twai_node_delete(self->node);
            self->node_created = false;
            self->node = NULL;
        }

        if (self->rx_fifo_storage)
        {
            free(self->rx_fifo_storage);
            self->rx_fifo_storage = NULL;
        }
        dtringfifo_reset(&self->rx_fifo);
    }
    return dterr;
}

// ----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_canbus_detach(dtiox_espidf_canbus_t* self DTIOX_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_enabled = false;

    if (self->node_created)
    {
        if (self->node_enabled)
        {
            DTMC_ESPIDF_C(twai_node_disable(self->node));
            self->node_enabled = false;
        }

        // unregister all event callbacks
        {
            twai_event_callbacks_t callbacks = { 0 };

            DTMC_ESPIDF_C(twai_node_register_event_callbacks(self->node, &callbacks, self));
        }

        DTMC_ESPIDF_C(twai_node_delete(self->node));
        self->node_created = false;
        self->node = NULL;
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Enable / disable
//
// Here, enabled controls both:
//  - twai_node_enable()/twai_node_disable()
//  - whether RX ISR pushes into FIFO (rx_enabled)
//
// We also clear FIFO, overflow and counters each time.

dterr_t*
dtiox_espidf_canbus_enable(dtiox_espidf_canbus_t* self DTIOX_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_enabled = enabled;

    if (!self->node_created)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "TWAI node not created");
        goto cleanup;
    }

    if (enabled && !self->node_enabled)
    {
        // Clear FIFO and counters.
        dtringfifo_reset(&self->rx_fifo);
        if (self->rx_fifo_storage)
        {
            dtringfifo_config_t fifo_cfg = {
                .buffer = self->rx_fifo_storage,
                .capacity = _rx_fifo_capacity_from_config(self),
            };
            (void)dtringfifo_configure(&self->rx_fifo, &fifo_cfg);
        }

        self->rx_overflow_flag = false;
        self->fifo_dropped_bytes = 0;
        self->rx_frames = 0;
        self->rx_bytes = 0;
        self->tx_frames = 0;
        self->tx_bytes = 0;
        self->alert_rx_done = 0;
        self->alert_tx_done = 0;
        self->alert_error = 0;

        // TODO: Consider if dtiox_espidf_canbus_enable() should clear old/new_state.

        DTMC_ESPIDF_C(twai_node_enable(self->node));
        self->node_enabled = true;
    }
    else if (!enabled && self->node_enabled)
    {
        DTMC_ESPIDF_C(twai_node_disable(self->node));
        self->node_enabled = false;
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Non-blocking read from FIFO
//
// Semantics:
// - If an RX overflow has occurred earlier, the *next* read returns an error
//   and clears the overflow flag.
// - Otherwise, pop as many bytes as available up to buf_len, non-blocking.

dterr_t*
dtiox_espidf_canbus_read(dtiox_espidf_canbus_t* self DTIOX_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);

    *out_read = 0;

    if (!self->node_created || !self->node_enabled)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN node not enabled");
        goto cleanup;
    }

    DTERR_C(_dtiox_espidf_canbus_check_error_state(self));

    // Overflow is reported once on the next read.
    if (self->rx_overflow_flag)
    {
        self->rx_overflow_flag = false;
        dterr =
          dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "RX FIFO overflow (dropped %" PRId32 " bytes)", self->fifo_dropped_bytes);
        goto cleanup;
    }

    int32_t n = _rx_fifo_pop(self, buf, buf_len);
    if (n < 0)
        n = 0;

    *out_read = n;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Non-blocking write
//
// Semantics:
// - Presents dtiox as a byte stream on top of CAN frames.
// - We split the buffer into up to 8-byte chunks and send frames sequentially.
// - twai_node_transmit(..., 0) => no wait; if TX queue is full, we stop
//   and return the number of bytes successfully queued so far.

dterr_t*
dtiox_espidf_canbus_write(dtiox_espidf_canbus_t* self DTIOX_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);

    *out_written = 0;

    if (!self->node_created || !self->node_enabled)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN node not enabled");
        goto cleanup;
    }

    DTERR_C(_dtiox_espidf_canbus_check_error_state(self));

    int32_t total_written = 0;

    while (total_written < len)
    {
        uint8_t data[8] = { 0 };
        int32_t chunk_len = len - total_written;
        if (chunk_len > 8)
            chunk_len = 8;

        memcpy(data, buf + total_written, (size_t)chunk_len);

        twai_frame_t msg = {
            .header.id = self->config.tx_identifier,
            .buffer = data,
            .buffer_len = (size_t)chunk_len,
        };

        // Extended vs standard ID
        msg.header.ide = self->config.use_extended_id ? 1 : 0;
        msg.header.rtr = 0; // data frame

        // Non-blocking transmit; DTMC_ESPIDF_C will propagate error
        DTMC_ESPIDF_C(twai_node_transmit(self->node, &msg, 0));
        // block until frame is sent to bus
        // TODO: dtiox_espidf_canbus_write should be able to absorb least some frames
        DTMC_ESPIDF_C(twai_node_transmit_wait_all_done(self->node, -1));

        total_written += chunk_len;
        _counter_add(&self->tx_frames, 1);
        _counter_add(&self->tx_bytes, chunk_len);
    }

    *out_written = total_written;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_canbus_set_rx_semaphore(dtiox_espidf_canbus_t* self DTIOX_SET_RX_SEMAPHORE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_semaphore = rx_semaphore;

    // NOTE: Currently the ISR does not post this semaphore; callers can
    // poll read() or extend the ISR to use an ISR-safe post routine.
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_canbus_concat_format(dtiox_espidf_canbus_t* self DTIOX_CONCAT_FORMAT_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    int32_t capacity = _rx_fifo_capacity_from_config(self);

    const char* enabled_str = self->node_enabled ? "enabled" : "disabled";

    *out_str = dtstr_concat_format(in_str,
      separator,
      "espidf-can (%" PRId32 ") tx_gpio=%d rx_gpio=%d bitrate=%d id=0x%08" PRIx32 " %s "
      "rx_ring=%" PRId32 " rx_frames=%" PRId32 " rx_bytes=%" PRId32 " tx_frames=%" PRId32 " tx_bytes=%" PRId32
      " alerts[rx=%" PRId32 ",tx=%" PRId32 ",err=%" PRId32 "] node=%s",
      self->model_number,
      4,
      5,
      250000,
      self->config.tx_identifier,
      self->config.use_extended_id ? "extended" : "standard",
      capacity,
      self->rx_frames,
      self->rx_bytes,
      self->tx_frames,
      self->tx_bytes,
      self->alert_rx_done,
      self->alert_tx_done,
      self->alert_error,
      enabled_str);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Dispose
//
// - Disable and delete the TWAI node.
// - Free FIFO storage.

void
dtiox_espidf_canbus_dispose(dtiox_espidf_canbus_t* self)
{
    if (!self)
        return;

    self->rx_enabled = false;

    if (self->node_created)
    {
        if (self->node_enabled)
        {
            (void)twai_node_disable(self->node);
            self->node_enabled = false;
        }
        (void)twai_node_delete(self->node);
        self->node_created = false;
        self->node = NULL;
    }

    if (self->rx_fifo_storage)
    {
        free(self->rx_fifo_storage);
        self->rx_fifo_storage = NULL;
    }

    dtringfifo_reset(&self->rx_fifo);
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtiox_espidf_canbus_copy(dtiox_espidf_canbus_t* this, dtiox_espidf_canbus_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtiox_espidf_canbus_equals(dtiox_espidf_canbus_t* a, dtiox_espidf_canbus_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtiox_espidf_canbus_equals backend.
    return (a->model_number == b->model_number &&                 //
            a->config.tx_identifier == b->config.tx_identifier && //
            a->config.tx_gpio_num == b->config.tx_gpio_num &&     //
            a->config.rx_gpio_num == b->config.rx_gpio_num);
}

// --------------------------------------------------------------------------------------------
const char*
dtiox_espidf_canbus_get_class(dtiox_espidf_canbus_t* self)
{
    return "dtiox_espidf_canbus_t";
}

// --------------------------------------------------------------------------------------------

bool
dtiox_espidf_canbus_is_iface(dtiox_espidf_canbus_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTIOX_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtiox_espidf_canbus_to_string(dtiox_espidf_canbus_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    snprintf(buffer,
      buffer_size,
      "id%" PRIX32 "-tx%" PRId32 "-rx%" PRId32 "@%" PRId32,
      self->config.tx_identifier,
      self->config.tx_gpio_num,
      self->config.rx_gpio_num,
      self->config.bitrate);
    buffer[buffer_size - 1] = '\0';
}
