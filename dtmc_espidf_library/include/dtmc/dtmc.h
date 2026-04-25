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

#define DTMC_FLAVOR DTMC_ESPIDF_FLAVOR

#define DTMC_VERSION_MAJOR DTMC_ESPIDF_VERSION_MAJOR
#define DTMC_VERSION_MINOR DTMC_ESPIDF_VERSION_MINOR
#define DTMC_VERSION_PATCH DTMC_ESPIDF_VERSION_PATCH

#define DTMC_VERSION DTMC_ESPIDF_VERSION