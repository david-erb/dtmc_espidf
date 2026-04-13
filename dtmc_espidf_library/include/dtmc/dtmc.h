/*
 * dtmc -- Canonical top-level include for the ESP-IDF platform library.
 *
 * Re-exports the full dtmc_espidf interface under the platform-agnostic
 * name <dtmc/dtmc.h>. Application code includes this header to remain
 * decoupled from the underlying platform selection while still gaining
 * access to flavor strings, QEMU detection, esp_err_t macros, and the
 * task-registry entry point provided by dtmc_espidf.
 *
 * cdox v1.0.2
 */
#pragma once
#include <dtmc/dtmc_espidf.h>