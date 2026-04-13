/*
 * dtmc_espidf -- ESP-IDF platform entry point for the dtmc embedded framework.
 *
 * Declares the platform flavor and version strings, a QEMU environment
 * detector, diagnostic helpers for printing environment and task state,
 * an error-log iterator compatible with dterr_each, and the task-registry
 * entry point that wires all ESP-IDF-specific tasks at startup. Also
 * provides DTMC_ESPIDF_C and DTMC_ESPIDF_PASS macros that convert esp_err_t
 * and FreeRTOS pdTRUE return values into dterr_t chains using the standard
 * goto-cleanup pattern.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>

#include <esp_err.h>
#include <esp_log.h>

#include <dtcore/dterr.h>
#include <dtmc/version.h>
#include <dtmc_base/dttasker_registry.h>

void
dtmc_espidf_each_error_log(dterr_t* dterr, void* context);

extern dterr_t*
dtmc_espidf_is_qemu(bool* is_qemu);
extern dterr_t*
dtmc_printf_environment(void);
extern dterr_t*
dtmc_printf_tasks(void);
extern dterr_t*
dtmc_register_tasks(dttasker_registry_t* registry);

// esp_err_t-based format string tokens
#define DTMC_ESPIDF_C_FORMAT "esp_err %d (%s)"
#define DTMC_ESPIDF_C_ARGS(ESP_ERR) (ESP_ERR), esp_err_to_name((ESP_ERR))

#define DTMC_ESPIDF_C(call)                                                                                                    \
    do                                                                                                                         \
    {                                                                                                                          \
        esp_err_t esp_err;                                                                                                     \
        if ((esp_err = (call)) != ESP_OK)                                                                                      \
        {                                                                                                                      \
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, #call " " DTMC_ESPIDF_C_FORMAT, DTMC_ESPIDF_C_ARGS(esp_err));       \
            goto cleanup;                                                                                                      \
        }                                                                                                                      \
    } while (0);

#define DTMC_ESPIDF_PASS(call)                                                                                                 \
    do                                                                                                                         \
    {                                                                                                                          \
        int result;                                                                                                            \
        if ((result = (call)) != pdTRUE)                                                                                       \
        {                                                                                                                      \
            dterr = dterr_new(DTERR_FAIL, __LINE__, __FILE__, __func__, NULL, "failed");                                       \
            goto cleanup;                                                                                                      \
        }                                                                                                                      \
    } while (0);
