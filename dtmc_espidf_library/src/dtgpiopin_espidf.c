// dtgpiopin_espidf.c — ESP-IDF backend for dtgpiopin on ESP32-class chips
//
// Uses simple GPIO numbering: config.pin_number is the ESP-IDF gpio_num_t value.
// Interrupts are configured for both edges; ISR inspects current level to decide
// between RISING vs FALLING.
//
// sdkconfig needs at least: CONFIG_GPIO=y, CONFIG_GPIOS_ISR=y (defaults on most boards)

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtmc_base/dtmc_base_constants.h>

#include <dtmc/dtgpiopin_espidf.h>
#include <dtmc_base/dtgpiopin.h>

#define TAG "dtgpiopin_espidf"

// ----------------------------- concrete object -----------------------------

struct dtgpiopin_espidf_t
{
    DTGPIOPIN_COMMON_MEMBERS;
    bool _is_malloced;

    dtgpiopin_espidf_config_t config;

    // ISR hookup
    dtgpiopin_isr_fn cb;
    void* cb_context;
    bool irq_enabled;
};

// ------------------------------- vtable glue -------------------------------

DTGPIOPIN_INIT_VTABLE(dtgpiopin_espidf)

// ------------------------------- ISR support -------------------------------

static bool s_isr_service_installed = false;

static dterr_t*
_ensure_isr_service_installed(void)
{
    dterr_t* dterr = NULL;

    if (s_isr_service_installed)
        return NULL;

    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE)
    {
        // Already installed elsewhere; treat as success.
        s_isr_service_installed = true;
        return NULL;
    }
    if (err != ESP_OK)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_install_isr_service err=%d (%s)", (int)err, esp_err_to_name(err));
        goto cleanup;
    }

    s_isr_service_installed = true;
cleanup:
    return dterr;
}

static void
_gpio_isr_thunk(void* arg)
{
    dtgpiopin_espidf_t* self = (dtgpiopin_espidf_t*)arg;
    if (!self || !self->cb || !self->irq_enabled)
        return;

    int level = gpio_get_level((gpio_num_t)self->config.pin_number);
    if (level < 0)
        return;

    dtgpiopin_edge_t edge = level ? DTGPIOPIN_IRQ_RISING : DTGPIOPIN_IRQ_FALLING;
    self->cb(edge, self->cb_context);
}

// ------------------------------- flag maps ---------------------------------

static inline gpio_mode_t
_map_mode(dtgpiopin_mode_t mode, dtgpiopin_drive_t drive)
{
    switch (mode)
    {
        case DTGPIOPIN_MODE_INPUT:
            return GPIO_MODE_INPUT;

        case DTGPIOPIN_MODE_OUTPUT:
            if (drive == DTGPIOPIN_DRIVE_OPEN_DRAIN)
                return GPIO_MODE_OUTPUT_OD;
            return GPIO_MODE_OUTPUT;

        case DTGPIOPIN_MODE_INOUT:
            if (drive == DTGPIOPIN_DRIVE_OPEN_DRAIN)
                return GPIO_MODE_INPUT_OUTPUT_OD;
            return GPIO_MODE_INPUT_OUTPUT;

        default:
            return GPIO_MODE_DISABLE;
    }
}

static inline void
_apply_drive_capability(uint8_t pin, dtgpiopin_drive_t drive)
{
#if SOC_GPIO_SUPPORT_PIN_GLITCH_FILTER || SOC_GPIO_SUPPORT_PIN_DRIVE_CAPABILITY
    gpio_drive_cap_t cap;

    switch (drive)
    {
        case DTGPIOPIN_DRIVE_WEAK:
            cap = GPIO_DRIVE_CAP_0;
            break;
        case DTGPIOPIN_DRIVE_STRONG:
            cap = GPIO_DRIVE_CAP_3;
            break;
        default:
            return; // DTGPIOPIN_DRIVE_DEFAULT / OPEN_DRAIN -> leave hardware default
    }

    (void)gpio_set_drive_capability((gpio_num_t)pin, cap);
#else
    (void)pin;
    (void)drive;
#endif
}

// --------------------------- lifecycle / factory ---------------------------

dterr_t*
dtgpiopin_espidf_create(dtgpiopin_espidf_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtgpiopin_espidf_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtgpiopin_espidf_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;
cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtgpiopin_espidf_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------------
dterr_t*
dtgpiopin_espidf_init(dtgpiopin_espidf_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_GPIOPIN_MODEL_ESPIDF;
    self->config.pin_number = 0;
    self->config.mode = DTGPIOPIN_MODE_INPUT;
    self->config.pull = DTGPIOPIN_PULL_NONE;
    self->config.drive = DTGPIOPIN_DRIVE_DEFAULT;

    DTERR_C(dtgpiopin_set_vtable(self->model_number, &dtgpiopin_espidf_vt));
cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtgpiopin_espidf_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------------
dterr_t*
dtgpiopin_espidf_configure(dtgpiopin_espidf_t* self, const dtgpiopin_espidf_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    if (config->pin_number >= DTGPIOPIN_ESPIDF_MAX_PIN)
    {
        return dterr_new(DTERR_FAIL,
          DTERR_LOC,
          NULL,
          "pin %u out of range [0..%u)",
          (unsigned)config->pin_number,
          (unsigned)DTGPIOPIN_ESPIDF_MAX_PIN);
    }

    self->config = *config;
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------------
dterr_t*
dtgpiopin_espidf_attach(dtgpiopin_espidf_t* self DTGPIOPIN_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    gpio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.pin_bit_mask = 1ULL << self->config.pin_number;
    cfg.mode = _map_mode(self->config.mode, self->config.drive);
    cfg.intr_type = GPIO_INTR_DISABLE; // enable() decides if interrupts are used

    switch (self->config.pull)
    {
        case DTGPIOPIN_PULL_UP:
            cfg.pull_up_en = 1;
            cfg.pull_down_en = 0;
            break;
        case DTGPIOPIN_PULL_DOWN:
            cfg.pull_up_en = 0;
            cfg.pull_down_en = 1;
            break;
        default:
            cfg.pull_up_en = 0;
            cfg.pull_down_en = 0;
            break;
    }

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_config pin=%u err=%d", (unsigned)self->config.pin_number, (int)err);

    _apply_drive_capability(self->config.pin_number, self->config.drive);

    self->cb = cb;
    self->cb_context = caller_context;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------------

dterr_t*
dtgpiopin_espidf_enable(dtgpiopin_espidf_t* self DTGPIOPIN_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    const gpio_num_t gpio = (gpio_num_t)self->config.pin_number;

    if (enable)
    {
        DTERR_C(_ensure_isr_service_installed());

        esp_err_t err = gpio_set_intr_type(gpio, GPIO_INTR_ANYEDGE);
        if (err != ESP_OK)
        {
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_set_intr_type err=%d", (int)err);
            goto cleanup;
        }

        err = gpio_isr_handler_add(gpio, _gpio_isr_thunk, self);
        if (err != ESP_OK)
        {
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_isr_handler_add err=%d", (int)err);
            goto cleanup;
        }

        self->irq_enabled = true;
    }
    else
    {
        self->irq_enabled = false;
        (void)gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
        (void)gpio_isr_handler_remove(gpio);
    }

cleanup:
    return dterr;
}

// ---------------------------------- read -----------------------------------

dterr_t*
dtgpiopin_espidf_read(dtgpiopin_espidf_t* self DTGPIOPIN_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_level);

    int level = gpio_get_level((gpio_num_t)self->config.pin_number);
    if (level < 0)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_get_level pin=%u err=%d", (unsigned)self->config.pin_number, level);
    }

    *out_level = (level != 0);
cleanup:
    return dterr;
}

// ---------------------------------- write ----------------------------------

dterr_t*
dtgpiopin_espidf_write(dtgpiopin_espidf_t* self DTGPIOPIN_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (!(self->config.mode == DTGPIOPIN_MODE_OUTPUT || self->config.mode == DTGPIOPIN_MODE_INOUT))
    {
        return dterr_new(
          DTERR_FAIL, DTERR_LOC, NULL, "write invalid in INPUT mode (pin=%u)", (unsigned)self->config.pin_number);
    }

    esp_err_t err = gpio_set_level((gpio_num_t)self->config.pin_number, level ? 1 : 0);
    if (err != ESP_OK)
    {
        return dterr_new(
          DTERR_FAIL, DTERR_LOC, NULL, "gpio_set_level pin=%u err=%d", (unsigned)self->config.pin_number, (int)err);
    }

cleanup:
    return dterr;
}

// --------------------------------- dispose ---------------------------------

void
dtgpiopin_espidf_dispose(dtgpiopin_espidf_t* self)
{
    if (!self)
        return;

    const gpio_num_t gpio = (gpio_num_t)self->config.pin_number;

    if (self->irq_enabled)
    {
        (void)gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
        (void)gpio_isr_handler_remove(gpio);
        self->irq_enabled = false;
    }

    self->cb = NULL;

    // Optionally reset pin back to default state
    (void)gpio_reset_pin(gpio);

    if (self->_is_malloced)
        free(self);
    else
        memset(self, 0, sizeof(*self));
}
