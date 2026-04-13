/*
 * dtiox_espidf_uart -- ESP-IDF UART backend for the dtiox byte-stream interface.
 *
 * Implements the dtiox vtable over an ESP-IDF UART port. Baud rate, parity,
 * data bits, stop bits, and flow control are set through the shared
 * dtuart_helper_config_t; GPIO pin assignments for TX, RX, RTS, and CTS
 * are provided separately. The UART port number is stored as a plain int
 * to avoid exposing uart_port_t in the public header.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtuart_helpers.h>

#include <dtmc_base/dtiox.h>

// Forward-declare concrete type
typedef struct dtiox_espidf_uart_t dtiox_espidf_uart_t;

typedef struct
{
    // ESP-IDF UART port number, e.g., 0, 1, 2 (cast to uart_port_t in .c)
    int32_t uart_port_num;

    dtuart_helper_config_t uart_config;

    // GPIO pin numbers (ESP-IDF-style, e.g., 1..39); use -1 to leave unchanged
    int32_t tx_pin;
    int32_t rx_pin;
    int32_t rts_pin; // -1 for unused
    int32_t cts_pin; // -1 for unused

} dtiox_espidf_uart_config_t;

// Lifecycle / configuration
extern dterr_t*
dtiox_espidf_uart_create(dtiox_espidf_uart_t** self_ptr);

extern dterr_t*
dtiox_espidf_uart_init(dtiox_espidf_uart_t* self);

extern dterr_t*
dtiox_espidf_uart_configure(dtiox_espidf_uart_t* self, const dtiox_espidf_uart_config_t* cfg);

// Interface plumbing (vtable targets)
DTIOX_DECLARE_API(dtiox_espidf_uart);
DTOBJECT_DECLARE_API(dtiox_espidf_uart);