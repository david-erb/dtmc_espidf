/*
 * dtnvblob_espidf_nvs -- ESP-IDF NVS backend for the dtnvblob persistent storage interface.
 *
 * Implements the dtnvblob vtable by reading and writing an opaque binary blob
 * to an ESP-IDF NVS namespace and key. Configuration requires only the NVS
 * namespace string and key name. The backend satisfies the same read/write/
 * dispose contract as other dtnvblob backends, allowing persistent storage
 * code to run unmodified across ESP-IDF and non-embedded targets.
 *
 * cdox v1.0.2
 */
#pragma once
// See markdown documentation at the end of this file.

// ESP-IDF NVS-backed implementation of dtnvblob.

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtnvblob.h>

// Back-end private plumbing config structure
typedef struct
{
    const char* nvs_namespace; // e.g. "storage"
    const char* key;           // e.g. "nvblob"
} dtnvblob_espidf_nvs_config_t;

// Forward declare the concrete espidf_nvs type
typedef struct dtnvblob_espidf_nvs_t dtnvblob_espidf_nvs_t;

extern dterr_t*
dtnvblob_espidf_nvs_create(dtnvblob_espidf_nvs_t** self_ptr);

extern dterr_t*
dtnvblob_espidf_nvs_init(dtnvblob_espidf_nvs_t* self);

extern dterr_t*
dtnvblob_espidf_nvs_configure(dtnvblob_espidf_nvs_t* self, dtnvblob_espidf_nvs_config_t* config);

// The espidf_nvs is a concrete implementation of the ::dtnvblob vtable-based interface.
// The macro below declares the public facade entry points (`read`, `write`, `dispose`)
// that down-cast the opaque handle and dispatch via this model's registered vtable.

// --------------------------------------------------------------------------------------

DTNVBLOB_DECLARE_API(dtnvblob_espidf_nvs) // < Declare facade glue for the espidf_nvs model.

#if MARKDOWN_DOCUMENTATION
// clang-format off
// --8<-- [start:markdown-documentation]
# dtnvblob_espidf_nvs

An ESP-IDF NVS-backed NVBLOB implementation that satisfies the **dtnvblob** vtable interface.

## Mini-guide

- Use `dtnvblob_espidf_nvs_create` for heap lifetime; use `dtnvblob_espidf_nvs_init` for stack/static lifetime.
- Apply configuration with `dtnvblob_espidf_nvs_configure` before first use (sets NVS namespace and key).
- Call via the common facade API (`dtnvblob_read`, `dtnvblob_write`, `dtnvblob_dispose`) once attached.
- Read contract:
  - If `blob == NULL`, `read` only reports the stored size in `*size`.
  - If `blob != NULL`, up to `*size` bytes are copied; if the stored blob is larger, the extra bytes are silently ignored (no error).
- Write contract:
  - Always overwrites any existing value for the configured key.
- Error contract: functions returning `dterr_t*` yield `NULL` on success; non-NULL on failure (caller owns the error).

## Sketch usage

```c
#include <dtmc_espidf/dtnvblob_espidf_nvs.h>
#include <dtcore/dterr.h>

void example(void)
{
    dterr_t* dterr = NULL;
    dtnvblob_handle nv = NULL;

    {
        dtnvblob_espidf_nvs_t* impl = NULL;
        DTERR_C(dtnvblob_espidf_nvs_create(&impl));
        dtnvblob_espidf_nvs_config_t cfg = {
            .nvs_namespace = "storage",
            .key = "nvblob0",
        };
        DTERR_C(dtnvblob_espidf_nvs_configure(impl, &cfg));
        nv = (dtnvblob_handle)impl;
    }

    // Query size
    int32_t size = 0;
    DTERR_C(dtnvblob_read(nv, NULL, &size));

    uint8_t* buffer = malloc((size_t)size);
    if (buffer == NULL)
        goto cleanup;

    // Read full blob
    DTERR_C(dtnvblob_read(nv, buffer, &size));

    // Write it back (or modified)
    int32_t to_write = size;
    DTERR_C(dtnvblob_write(nv, buffer, &to_write));

cleanup:
    dtnvblob_dispose(nv);
    free(buffer);
}
```
// --8<-- [end:markdown-documentation]
// clang-format on
#endif
