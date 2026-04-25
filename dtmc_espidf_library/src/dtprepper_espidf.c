#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_err.h>

#include <esp_event.h>
#include <esp_freertos_hooks.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <nvs.h>
#include <nvs_flash.h>

#include <lwip/err.h>
#include <lwip/sys.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtmc_base/dtradioconfig.h>

#include <dtmc/dtmc_espidf.h>

#include <dtmc/dtprepper_espidf.h>

#define TAG "dtprepper"

// -------------------------------------------------------------------------------
static dterr_t*
start_system(dtprepper_espidf_t* self)
{
    esp_err_t esp_err;
    dterr_t* dterr = NULL;

    dtlog_info(TAG, "starting system");
    dtlog_info(TAG, "starting nvs flash");
    esp_err = nvs_flash_init();
    if (esp_err == ESP_ERR_NVS_NO_FREE_PAGES || esp_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        DTMC_ESPIDF_C(nvs_flash_erase());
        DTMC_ESPIDF_C(nvs_flash_init());
    }

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------
static dterr_t*
start_station(dtprepper_espidf_t* self)
{
    dterr_t* dterr = NULL;

    dtlog_info(TAG, "starting esp_netif to set up network stacks");
    DTMC_ESPIDF_C(esp_netif_init());

    dtlog_info(TAG, "starting default esp_event_loop for wifi component");
    DTMC_ESPIDF_C(esp_event_loop_create_default());

    dtlog_info(TAG, "connecting wifi station");
    DTERR_C(self->station.connect(&self->station));

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------
static dterr_t*
start_ethernet(dtprepper_espidf_t* self)
{
    dterr_t* dterr = NULL;

    dtlog_info(TAG, "starting esp_netif to set up network stacks");
    DTMC_ESPIDF_C(esp_netif_init());

    dtlog_info(TAG, "starting default esp_event_loop for wifi component");
    DTMC_ESPIDF_C(esp_event_loop_create_default());

    dtlog_info(TAG, "connecting to ethernet");
    DTERR_C(self->ethernet.connect(&self->ethernet));

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------
static void
dispose(dtprepper_espidf_t* self)
{
    if (self == NULL)
        return;

    if (self->ethernet.dispose != NULL)
        self->ethernet.dispose(&self->ethernet);
    if (self->station.dispose != NULL)
        self->station.dispose(&self->station);

    // TODO: de-register the idle hook in dtprepper.dispose.

    dtradioconfig_dispose(&self->radioconfig);

    memset(self, 0, sizeof(*self));
}

// -------------------------------------------------------------------------------
static dterr_t*
configure(dtprepper_espidf_t* self)
{
    dterr_t* dterr = NULL;
    dtlog_info(TAG, "configuring dtprepper");

    self->station_config.radioconfig = &self->radioconfig;
    self->station_config.retry_count_maximum = 5;
    DTERR_C(self->station.configure(&self->station, &self->station_config));

    self->ethernet_config.radioconfig = &self->radioconfig;
    DTERR_C(self->ethernet.configure(&self->ethernet, &self->ethernet_config));

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "unable to finish configuring all the prepper's items");
        self->dispose(self);
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
dtprepper_espidf_init(dtprepper_espidf_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));

    // initialize method members
    self->configure = configure;
    self->start_system = start_system;
    self->start_station = start_station;
    self->start_ethernet = start_ethernet;
    self->dispose = dispose;

    DTERR_C(dtradioconfig_init(&self->radioconfig));
    DTERR_C(dtstation_init(&self->station));
    DTERR_C(dtethernet_init(&self->ethernet));

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "unable to finish initializing all the prepper's items");
        self->dispose(self);
    }

    return dterr;
}
