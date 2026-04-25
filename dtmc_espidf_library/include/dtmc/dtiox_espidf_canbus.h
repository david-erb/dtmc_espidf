/*
 * dtiox_espidf_canbus -- ESP-IDF TWAI (CAN) backend for the dtiox byte-stream interface.
 *
 * Implements the dtiox vtable over the ESP-IDF TWAI controller, presenting
 * CAN traffic as a plain byte stream: outbound bytes are sliced into 0-8-byte
 * CAN frames using a configurable TX identifier, and inbound frame payloads
 * are reassembled into an RX ring buffer. GPIO pins, bitrate, frame format
 * (11-bit or 29-bit), operating mode, and queue depths are all set through
 * dtiox_espidf_canbus_config_t without exposing any TWAI-specific types.
 *
 * cdox v1.0.2
 */
#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>

// Forward-declare concrete type
typedef struct dtiox_espidf_canbus_t dtiox_espidf_canbus_t;

/**
 * Public configuration for ESP-IDF CAN (TWAI) backend.
 *
 * NOTE:
 * - Only primitive data types here (int32_t, uint32_t, bool, const char*).
 * - No twai-specific types leak into this struct.
 *
 * This backend presents CAN as a simple byte-stream:
 * - Writes: bytes are sliced into 0–8-byte CAN frames.
 * - Reads: payload bytes are pushed into a FIFO in arrival order.
 */
typedef struct
{
    // GPIO pins for the on-chip CAN controller (TWAI).
    // These are the ESP32 GPIO numbers.
    int32_t tx_gpio_num; // e.g., 21
    int32_t rx_gpio_num; // e.g., 22

    // Bitrate in bits/second (supported: 125000, 250000, 500000, 1000000).
    int32_t bitrate;

    // CAN identifier used for transmitted frames.
    // The backend does not enforce any particular ID scheme; that's up to the application.
    uint32_t tx_identifier;

    // If true, use extended (29-bit) identifier; otherwise 11-bit standard identifier.
    bool use_extended_id;

    // Basic operation mode:
    // 0 => normal mode (active on bus)
    // 1 => listen-only (silent), receive only
    // 2 => no-ack mode (for self-test / single-node setups)
    int32_t mode;

    // TX and RX queue lengths for the TWAI driver.
    int32_t tx_queue_len; // 0 => use backend default, e.g. 16
    int32_t rx_queue_len; // 0 => use backend default, e.g. 16

    // RX ring buffer capacity in bytes.
    // 0 => use backend default (e.g., 1024 bytes).
    int32_t rx_ring_capacity;

} dtiox_espidf_canbus_config_t;

// Lifecycle and configuration
extern dterr_t*
dtiox_espidf_canbus_create(dtiox_espidf_canbus_t** self_ptr);

extern dterr_t*
dtiox_espidf_canbus_init(dtiox_espidf_canbus_t* self);

extern dterr_t*
dtiox_espidf_canbus_configure(dtiox_espidf_canbus_t* self, const dtiox_espidf_canbus_config_t* cfg);

// dtiox interface plumbing
DTIOX_DECLARE_API(dtiox_espidf_canbus);
DTOBJECT_DECLARE_API(dtiox_espidf_canbus);