#include <stdlib.h>
#include <string.h>

#include <nvs.h>
#include <nvs_flash.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtmc_base_constants.h>

#include <dtmc_base/dtnvblob.h>

#include <dtmc/dtnvblob_espidf_nvs.h>

DTNVBLOB_INIT_VTABLE(dtnvblob_espidf_nvs)

/*------------------------------------------------------------------------*/
// Concrete instance layout (private to this TU)
typedef struct dtnvblob_espidf_nvs_t
{
    DTNVBLOB_COMMON_MEMBERS;
    dtnvblob_espidf_nvs_config_t config;
    bool _is_malloced;
    bool _is_nvs_initialized;
} dtnvblob_espidf_nvs_t;

/*------------------------------------------------------------------------*/
static dterr_t*
dtnvblob_espidf_nvs__from_esp_err(esp_err_t err, const char* context)
{
    if (err == ESP_OK)
        return NULL;

    int code = DTERR_IO;
    if (err == ESP_ERR_NO_MEM)
        code = DTERR_NOMEM;
    else if (err == ESP_ERR_NVS_NOT_FOUND)
        code = DTERR_NOTFOUND;

    return dterr_new(code, DTERR_LOC, NULL, "%s (esp_err=0x%x)", context, (unsigned)err);
}

/*------------------------------------------------------------------------*/
static dterr_t*
dtnvblob_espidf_nvs__ensure_init(dtnvblob_espidf_nvs_t* self)
{
    dterr_t* dterr = NULL;
    if (self->_is_nvs_initialized)
        return NULL;

    // Ensure NVS is initialized. This is safe to call multiple times.
    esp_err_t err = nvs_flash_init();
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_flash_init failed");
    if (dterr != NULL)
        goto cleanup;

    self->_is_nvs_initialized = true;
cleanup:
    return dterr;
}

/*------------------------------------------------------------------------*/
dterr_t*
dtnvblob_espidf_nvs_create(dtnvblob_espidf_nvs_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtnvblob_espidf_nvs_t*)malloc(sizeof(dtnvblob_espidf_nvs_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM,
          DTERR_LOC,
          NULL,
          "failed to allocate %zu bytes for dtnvblob_espidf_nvs_t",
          sizeof(dtnvblob_espidf_nvs_t));
        goto cleanup;
    }

    DTERR_C(dtnvblob_espidf_nvs_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr != NULL)
    {
        if (self_ptr != NULL && *self_ptr != NULL)
        {
            free(*self_ptr);
            *self_ptr = NULL;
        }
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtnvblob_espidf_nvs_create failed");
    }
    return dterr;
}

/*------------------------------------------------------------------------*/
dterr_t*
dtnvblob_espidf_nvs_init(dtnvblob_espidf_nvs_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_NVBLOB_MODEL_ESPIDF_NVS; // define in litup_world.h

    // Publish vtable for this model
    DTERR_C(dtnvblob_set_vtable(self->model_number, &dtnvblob_espidf_nvs_vt));

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtnvblob_espidf_nvs_init failed");
    return dterr;
}

/*------------------------------------------------------------------------*/
dterr_t*
dtnvblob_espidf_nvs_configure(dtnvblob_espidf_nvs_t* self, dtnvblob_espidf_nvs_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->nvs_namespace);
    DTERR_ASSERT_NOT_NULL(config->key);

    self->config = *config;

cleanup:
    return dterr;
}

/*------------------------------------------------------------------------*/
dterr_t*
dtnvblob_espidf_nvs_read(dtnvblob_espidf_nvs_t* self DTNVBLOB_READ_ARGS)
{
    dterr_t* dterr = NULL;
    nvs_handle_t handle = 0;
    esp_err_t err = ESP_OK;
    uint8_t* tmp = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(size);

    // Ensure NVS is initialized.
    DTERR_C(dtnvblob_espidf_nvs__ensure_init(self));

    err = nvs_open(self->config.nvs_namespace, NVS_READONLY, &handle);
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_open failed");
    if (dterr != NULL)
        goto cleanup;

    size_t required_size = 0;
    err = nvs_get_blob(handle, self->config.key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        dterr = dterr_new(DTERR_NOTFOUND,
          DTERR_LOC,
          NULL,
          "key '%s' not found in namespace '%s'",
          self->config.key,
          self->config.nvs_namespace);
        goto cleanup;
    }
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_get_blob(size) failed");
    if (dterr != NULL)
        goto cleanup;

    // If blob is NULL, treat as size query: just return stored size.
    if (blob == NULL)
    {
        if (required_size > INT32_MAX)
        {
            dterr = dterr_new(DTERR_IO, DTERR_LOC, NULL, "stored blob too large (%u bytes)", (unsigned)required_size);
            goto cleanup;
        }

        *size = (int32_t)required_size;
        goto cleanup;
    }

    // We need to fulfill the "no error if blob does not fit" contract.
    int32_t capacity = *size;
    if (capacity < 0)
        capacity = 0;

    if (required_size > (size_t)INT32_MAX)
    {
        dterr = dterr_new(DTERR_IO, DTERR_LOC, NULL, "stored blob too large (%u bytes)", (unsigned)required_size);
        goto cleanup;
    }

    size_t cap_size = (size_t)capacity;
    if (cap_size >= required_size)
    {
        // Caller buffer is large enough, read directly into it.
        size_t len = required_size;
        err = nvs_get_blob(handle, self->config.key, blob, &len);
        dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_get_blob(data) failed");
        if (dterr != NULL)
            goto cleanup;

        *size = (int32_t)len;
    }
    else
    {
        // Caller buffer is smaller than stored blob: read into temp and truncate,
        // without surfacing an error to the caller.
        tmp = (uint8_t*)malloc(required_size);
        if (tmp == NULL)
        {
            dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %u bytes", (unsigned)required_size);
            goto cleanup;
        }

        size_t len = required_size;
        err = nvs_get_blob(handle, self->config.key, tmp, &len);
        dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_get_blob(tmp) failed");
        if (dterr != NULL)
            goto cleanup;

        if (cap_size > 0)
            memcpy(blob, tmp, cap_size);

        *size = (int32_t)cap_size;
    }

cleanup:
    if (handle != 0)
        nvs_close(handle);

    if (tmp != NULL)
        free(tmp);

    return dterr;
}

/*------------------------------------------------------------------------*/
dterr_t*
dtnvblob_espidf_nvs_write(dtnvblob_espidf_nvs_t* self DTNVBLOB_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    nvs_handle_t handle = 0;
    esp_err_t err = ESP_OK;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(blob);
    DTERR_ASSERT_NOT_NULL(size);

    // Ensure NVS is initialized.
    DTERR_C(dtnvblob_espidf_nvs__ensure_init(self));

    err = nvs_open(self->config.nvs_namespace, NVS_READWRITE, &handle);
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_open failed");
    if (dterr != NULL)
        goto cleanup;

    int32_t to_write = *size;
    if (to_write < 0)
        to_write = 0;

    err = nvs_set_blob(handle, self->config.key, blob, (size_t)to_write);
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_set_blob failed");
    if (dterr != NULL)
        goto cleanup;

    err = nvs_commit(handle);
    dterr = dtnvblob_espidf_nvs__from_esp_err(err, "nvs_commit failed");
    if (dterr != NULL)
        goto cleanup;

    *size = to_write;

cleanup:
    if (handle != 0)
        nvs_close(handle);

    return dterr;
}

/*------------------------------------------------------------------------*/
void
dtnvblob_espidf_nvs_dispose(dtnvblob_espidf_nvs_t* self)
{
    if (self == NULL)
        return;

    if (self->_is_malloced)
    {
        free(self);
    }
    else
    {
        memset(self, 0, sizeof(*self));
    }
}
