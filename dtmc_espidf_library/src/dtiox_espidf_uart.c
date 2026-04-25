// ESP-IDF backend for dtiox

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dtuart_helpers.h>

#include <dtmc/dtiox_espidf_uart.h>

#include <dtmc/dtmc.h>

#define TAG "dtiox_espidf_uart"
#define dtlog_debug(TAG, ...)

// vtable
DTIOX_INIT_VTABLE(dtiox_espidf_uart);
DTOBJECT_INIT_VTABLE(dtiox_espidf_uart);

// Tunable defaults
#define DTIOX_ESPIDF_UART_RX_TASK_STACK (4096)
#define DTIOX_ESPIDF_UART_RX_TASK_PRIORITY (DTTASKER_PRIORITY_URGENT_MEDIUM)
#define DTIOX_ESPIDF_UART_RX_BUF_SIZE (1024)
#define DTIOX_ESPIDF_UART_TX_BUF_SIZE (1024)
#define DTIOX_ESPIDF_UART_QUEUE_LEN (20)

// -----------------------------------------------------------------------------
// Concrete type

typedef struct dtiox_espidf_uart_t
{
    DTIOX_COMMON_MEMBERS
    bool _is_malloced;

    dtiox_espidf_uart_config_t config;

    uart_port_t port;
    QueueHandle_t uart_queue;

    // Semaphore to signal new data to user land
    dtsemaphore_handle rx_semaphore;

    // RX task control
    dttasker_handle rx_tasker_handle;

    // statistics we gather
    int32_t rx_overflow_count;

    // lightweight statistics
    struct
    {
        int32_t rx_bytes;  // total bytes returned to caller via read()
        int32_t tx_bytes;  // total bytes accepted by driver via write()
        int32_t rx_reads;  // number of read() calls
        int32_t tx_writes; // number of write() calls
    } stats;

} dtiox_espidf_uart_t;

// -----------------------------------------------------------------------------
// Helpers to map facade enums to ESP-IDF enums

static uart_parity_t
_map_parity(dtuart_parity_t p)
{
    switch (p)
    {
        case DTUART_PARITY_NONE:
            return UART_PARITY_DISABLE;
        case DTUART_PARITY_EVEN:
            return UART_PARITY_EVEN;
        case DTUART_PARITY_ODD:
            return UART_PARITY_ODD;
        default:
            return UART_PARITY_DISABLE;
    }
}

static uart_word_length_t
_map_data_bits(dtuart_data_bits_t d)
{
    switch (d)
    {
        case DTUART_DATA_BITS_7:
            return UART_DATA_7_BITS;
        case DTUART_DATA_BITS_8:
        default:
            return UART_DATA_8_BITS;
    }
}

static uart_stop_bits_t
_map_stop_bits(dtuart_stopbits_t s)
{
    switch (s)
    {
        case DTUART_STOPBITS_2:
            return UART_STOP_BITS_2;
        case DTUART_STOPBITS_1:
        default:
            return UART_STOP_BITS_1;
    }
}

static uart_hw_flowcontrol_t
_map_flow(dtuart_flow_t f)
{
    switch (f)
    {
        case DTUART_FLOW_RTSCTS:
            return UART_HW_FLOWCTRL_CTS_RTS;
        case DTUART_FLOW_NONE:
        default:
            return UART_HW_FLOWCTRL_DISABLE;
    }
}

// -----------------------------------------------------------------------------
// RX task

static dterr_t*
_rx_task_entry(void* arg, dttasker_handle tasker_handle)
{
    dterr_t* dterr = NULL;
    dtiox_espidf_uart_t* self = (dtiox_espidf_uart_t*)arg;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->uart_queue);

    // signal we are ready for business to the process who started us
    DTERR_C(dttasker_ready(tasker_handle));

    uart_event_t event;

    for (;;)
    {
        if (xQueueReceive(self->uart_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        if (event.type == UART_DATA)
        {
            if (self->rx_semaphore)
            {
                DTERR_C(dtsemaphore_post(self->rx_semaphore));
            }
        }
        else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL)
        {
            DTCORE_HELPER_INC32(self->rx_overflow_count);
        }
        else if (event.type == UART_BREAK)
        {
            dterr = dterr_new(DTERR_INTERNAL, DTERR_LOC, NULL, "UART BREAK detected on port %d", (int)self->port);
            goto cleanup;
        }
        else if (event.type == UART_PARITY_ERR)
        {
            dterr = dterr_new(DTERR_INTERNAL, DTERR_LOC, NULL, "UART parity error event on port %d", (int)self->port);
            goto cleanup;
        }
        else if (event.type == UART_FRAME_ERR)
        {
            dterr = dterr_new(DTERR_INTERNAL, DTERR_LOC, NULL, "UART frame error event on port %d", (int)self->port);
            goto cleanup;
        }
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Lifecycle

dterr_t*
dtiox_espidf_uart_create(dtiox_espidf_uart_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtiox_espidf_uart_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtiox_espidf_uart_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_espidf_uart_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_uart_init(dtiox_espidf_uart_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ESPIDF_UART;

    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_espidf_uart_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtiox_espidf_uart_object_vt));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_espidf_uart_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_uart_configure(dtiox_espidf_uart_t* self, const dtiox_espidf_uart_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    // validate UART config
    DTERR_C(dtuart_helper_validate(&config->uart_config));

    self->config = *config;

    self->port = (uart_port_t)self->config.uart_port_num;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Vtable targets

dterr_t*
dtiox_espidf_uart_attach(dtiox_espidf_uart_t* self DTIOX_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (self->uart_queue != NULL)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "UART already attached on port %d", (int)self->port);
        goto cleanup;
    }

    // Configure UART parameters
    uart_config_t ucfg = {
        .baud_rate = (int)self->config.uart_config.baudrate,
        .data_bits = _map_data_bits(self->config.uart_config.data_bits),
        .parity = _map_parity(self->config.uart_config.parity),
        .stop_bits = _map_stop_bits(self->config.uart_config.stop_bits),
        .flow_ctrl = _map_flow(self->config.uart_config.flow),
        .source_clk = UART_SCLK_DEFAULT,
    };

    DTMC_ESPIDF_C(uart_param_config(self->port, &ucfg));

    int32_t tx = self->config.tx_pin;
    int32_t rx = self->config.rx_pin;
    int32_t rts = self->config.rts_pin;
    int32_t cts = self->config.cts_pin;

    if (tx < 0)
        tx = UART_PIN_NO_CHANGE;
    if (rx < 0)
        rx = UART_PIN_NO_CHANGE;
    if (rts < 0)
        rts = UART_PIN_NO_CHANGE;
    if (cts < 0)
        cts = UART_PIN_NO_CHANGE;

    DTMC_ESPIDF_C(uart_set_pin(self->port, tx, rx, rts, cts));

    // Install driver with RX/TX buffers and event queue
    DTMC_ESPIDF_C(uart_driver_install(self->port,
      DTIOX_ESPIDF_UART_RX_BUF_SIZE,
      DTIOX_ESPIDF_UART_TX_BUF_SIZE,
      DTIOX_ESPIDF_UART_QUEUE_LEN,
      &self->uart_queue,
      0));

    // Start RX task to loop on uart_queue
    {
        dttasker_config_t config = { 0 };
        config.name = "uart_rx";
        config.tasker_entry_point_fn = _rx_task_entry;
        config.tasker_entry_point_arg = self;
        config.stack_size = DTIOX_ESPIDF_UART_RX_TASK_STACK;
        config.priority = DTIOX_ESPIDF_UART_RX_TASK_PRIORITY;
        config.core = 0;
        DTERR_C(dttasker_create(&self->rx_tasker_handle, &config));
        DTERR_C(dttasker_start(self->rx_tasker_handle));
    }

cleanup:
    return dterr;
}

// ----------------------------------------------------------------------------
dterr_t*
dtiox_espidf_uart_detach(dtiox_espidf_uart_t* self DTIOX_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Enable / disable
//
// NOTE:
// - This does NOT start/stop the RX task or driver.
// - It only controls whether the interrupt handler is enabled for RX.
// - It also clears FIFO and counters on each call.

dterr_t*
dtiox_espidf_uart_enable(dtiox_espidf_uart_t* self DTIOX_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->uart_queue);

    if (enabled)
    {
        uart_enable_rx_intr(self->port);
    }
    else
    {
        uart_disable_rx_intr(self->port);
    }

    uart_flush_input(self->port);
    xQueueReset(self->uart_queue);

    // *** NEW ***: reset statistics when (re)enabling
    self->rx_overflow_count = 0;
    self->stats.rx_bytes = 0;
    self->stats.tx_bytes = 0;
    self->stats.rx_reads = 0;
    self->stats.tx_writes = 0;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_uart_read(dtiox_espidf_uart_t* self DTIOX_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);

    // *** NEW ***: count read() calls
    DTCORE_HELPER_INC32(self->stats.rx_reads);

    // if RX task died, surface this as an error
    {
        dttasker_info_t task_info;
        dterr = dttasker_get_info(self->rx_tasker_handle, &task_info);
        if (dterr == NULL)
            dterr = task_info.dterr;
        if (dterr != NULL || task_info.status != RUNNING)
        {
            dterr = dterr_new(DTERR_FAIL,
              DTERR_LOC,
              dterr,
              "UART RX task state %s error %p",
              dttasker_state_to_string(task_info.status),
              task_info.dterr);
            goto cleanup;
        }
    }

    // overflow is reported once on the next read
    if (self->rx_overflow_count > 0)
    {
        dterr = dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "RX FIFO overflow has occurred earlier");
        goto cleanup;
    }

    // Non-blocking read: timeout 0
    int32_t n = uart_read_bytes(self->port, buf, buf_len, 0);

    dtlog_debug(TAG, "dtiox_espidf_uart_read port=%d requested=%" PRId32 " read=%" PRId32, (int)self->port, buf_len, n);

    if (n < 0)
    {
        *out_read = 0;
    }
    else
    {
        *out_read = n;
        DTCORE_HELPER_ADD32(self->stats.rx_bytes, n);
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_uart_write(dtiox_espidf_uart_t* self DTIOX_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);

    // *** NEW ***: count write() calls
    DTCORE_HELPER_INC32(self->stats.tx_writes);

    int n = uart_write_bytes(self->port, buf, len);
    if (n < 0)
    {
        *out_written = 0;
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "uart_write_bytes failed port=%d", (int)self->port);
        goto cleanup;
    }

    *out_written = (size_t)n;

    // *** NEW ***: accumulate bytes accepted by driver
    if (n > 0)
    {
        if (n > INT32_MAX)
            n = INT32_MAX;
        DTCORE_HELPER_ADD32(self->stats.tx_bytes, (int32_t)n);
    }

    // block until transmitted
    // TODO: Consdider if dtiox_espidf_uart_write needs to wait for TX done.
    DTMC_ESPIDF_C(uart_wait_tx_done(self->port, portMAX_DELAY));

cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_espidf_uart_set_rx_semaphore(dtiox_espidf_uart_t* self DTIOX_SET_RX_SEMAPHORE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_semaphore = rx_semaphore;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_espidf_uart_concat_format(dtiox_espidf_uart_t* self DTIOX_CONCAT_FORMAT_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    dttasker_info_t task_info = { 0 };
    if (self->rx_tasker_handle)
        DTERR_C(dttasker_get_info(self->rx_tasker_handle, &task_info));

    // instantaneous RX FIFO depth from driver
    size_t rx_fifo_len = 0;
    if (uart_get_buffered_data_len(self->port, &rx_fifo_len) != ESP_OK)
        rx_fifo_len = 0;

    char tmp[64];
    dtuart_helper_to_string(&self->config.uart_config, tmp, sizeof(tmp));

    *out_str = dtstr_concat_format(in_str,
      separator,
      "espidf (%" PRId32 ") port=%ld %s "
      "tx=%ld rx=%ld rts=%ld cts=%ld rx_task_status=%s "
      "rx_bytes=%" PRId32 " tx_bytes=%" PRId32 " rx_reads=%" PRId32 " tx_writes=%" PRId32 " rx_overflow=%" PRId32
      " rx_fifo_now=%u",
      self->model_number,
      (long)self->config.uart_port_num,
      tmp,
      (long)self->config.tx_pin,
      (long)self->config.rx_pin,
      (long)self->config.rts_pin,
      (long)self->config.cts_pin,
      dttasker_state_to_string(task_info.status),
      self->stats.rx_bytes,
      self->stats.tx_bytes,
      self->stats.rx_reads,
      self->stats.tx_writes,
      self->rx_overflow_count,
      (unsigned)rx_fifo_len);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
//
// In practical use, this backend is expected to be started once and run until
// reboot. dispose() is mainly for tests or error paths.
//
// - On dispose, the task might still be running, so leave the "self" resource intact.

void
dtiox_espidf_uart_dispose(dtiox_espidf_uart_t* self)
{
    if (!self)
        return;

    if (self->uart_queue)
    {
        // Driver uninstall will free ISR + queue
        uart_driver_delete(self->port);
        self->uart_queue = NULL;
    }
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtiox_espidf_uart_copy(dtiox_espidf_uart_t* this, dtiox_espidf_uart_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtiox_espidf_uart_equals(dtiox_espidf_uart_t* a, dtiox_espidf_uart_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtiox_espidf_uart_equals backend.
    return (a->model_number == b->model_number &&                                 //
            a->config.uart_port_num == b->config.uart_port_num &&                 //
            a->config.uart_config.baudrate == b->config.uart_config.baudrate &&   //
            a->config.uart_config.parity == b->config.uart_config.parity &&       //
            a->config.uart_config.data_bits == b->config.uart_config.data_bits && //
            a->config.uart_config.stop_bits == b->config.uart_config.stop_bits && //
            a->config.uart_config.flow == b->config.uart_config.flow);
}

// --------------------------------------------------------------------------------------------
const char*
dtiox_espidf_uart_get_class(dtiox_espidf_uart_t* self)
{
    return "dtiox_espidf_uart_t";
}

// --------------------------------------------------------------------------------------------

bool
dtiox_espidf_uart_is_iface(dtiox_espidf_uart_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTIOX_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtiox_espidf_uart_to_string(dtiox_espidf_uart_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    char tmp[128];
    dtuart_helper_to_string(&self->config.uart_config, tmp, sizeof(tmp));

    snprintf(buffer,
      buffer_size,
      "uart%" PRId32 " rxpin=%" PRId32 " txpin=%" PRId32 " %s",
      self->config.uart_port_num,
      self->config.rx_pin,
      self->config.tx_pin,
      tmp);
    buffer[buffer_size - 1] = '\0';
}
