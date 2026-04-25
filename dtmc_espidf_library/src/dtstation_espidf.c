#include <string.h>

#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <lwip/err.h>
#include <lwip/sys.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtmc_base/dtradioconfig.h>

#include <dtmc/dtstation_espidf.h>

#include <dtmc/dtmc_espidf.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define TAG "dtstation"

// ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD
// ESP_WIFI_SAE_MODE
// EXAMPLE_H2E_IDENTIFIER

// -----------------------------------------------------------------------------
// dtstation configure function
static dterr_t*
dtstation_configure(dtstation_espidf_t* this, dtstation_espidf_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(this);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->radioconfig);
    DTERR_ASSERT_NOT_NULL(config->radioconfig->wifi_ssid);
    DTERR_ASSERT_NOT_NULL(config->radioconfig->wifi_password);

    if (config->radioconfig->wifi_ssid[0] == 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "invalid empty wifi_ssid in config->radioconfig");
        goto cleanup;
    }
    if (config->radioconfig->wifi_password[0] == 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "invalid empty wifi_password in config->radioconfig");
        goto cleanup;
    }

    this->config = config;
cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to configure wifi station");
    return dterr;
}

// -----------------------------------------------------------------------------
static void
event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    dtstation_espidf_t* this = (dtstation_espidf_t*)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        dtlog_info(TAG, "got WIFI_EVENT_STA_START, calling esp_wifi_connect()");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (this->config->retry_count_maximum == 0 || this->retry_count < this->config->retry_count_maximum)
        {
            dtlog_info(TAG,
              "got WIFI_EVENT_STA_DISCONNECTED at retry %d, calling "
              "esp_wifi_connect()",
              this->retry_count);
            esp_wifi_connect();
            this->retry_count++;
        }
        else
        {
            dtlog_info(TAG, "got WIFI_EVENT_STA_DISCONNECTED at retry %d, giving up", this->retry_count);
            xEventGroupSetBits(this->event_group_handle, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        dtlog_info(TAG, "got IP_EVENT_STA_GOT_IP ip is: " IPSTR, IP2STR(&event->ip_info.ip));
        this->retry_count = 0;
        xEventGroupSetBits(this->event_group_handle, WIFI_CONNECTED_BIT);
    }
}

// -----------------------------------------------------------------------------
// dtstation connection function
static dterr_t*
dtstation_connect(dtstation_espidf_t* this)
{
    dterr_t* dterr = NULL;

    dtlog_info(TAG, "connecting wifi station to \"%s\"", this->config->radioconfig->wifi_ssid);
    dtlog_info(TAG, "creating default wifi station");
    esp_netif_create_default_wifi_sta();

    dtlog_info(TAG, "initializing wifi");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    DTMC_ESPIDF_C(esp_wifi_init(&cfg));

    dtlog_info(TAG, "initializing wifi 2");
    esp_event_handler_instance_t instance_any_id;
    DTMC_ESPIDF_C(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, &instance_any_id));

    dtlog_info(TAG, "initializing wifi 3");
    esp_event_handler_instance_t instance_got_ip;
    DTMC_ESPIDF_C(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };

    dtlog_info(TAG, "initializing wifi 4");
    // set Wi-Fi configuration with credentials from radioconfig
    strncpy((char*)wifi_config.sta.ssid, (char*)this->config->radioconfig->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    dtlog_info(TAG, "initializing wifi 5");
    strncpy(
      (char*)wifi_config.sta.password, (char*)this->config->radioconfig->wifi_password, sizeof(wifi_config.sta.password) - 1);

    dtlog_info(TAG, "initializing wifi 6");
    DTMC_ESPIDF_C(esp_wifi_set_mode(WIFI_MODE_STA));
    dtlog_info(TAG, "initializing wifi 7");
    DTMC_ESPIDF_C(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    dtlog_info(TAG, "starting wifi task");
    DTMC_ESPIDF_C(esp_wifi_start());
    dtlog_info(TAG, "wifi task has started");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // wait for connection
    bool all_done;

    int32_t timeout_ms = 10000; // 10 seconds

    EventBits_t uxBits = xEventGroupWaitBits(this->event_group_handle, // The event group handle.
      WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,                              // Bits to wait for.
      pdFALSE,                                                         // Do not clear the bit on exit.
      pdFALSE,                                                         // Wait for either bit, not both.
      pdMS_TO_TICKS(timeout_ms));

    this->connection_failed = !!(uxBits & WIFI_FAIL_BIT);
    all_done = !!(uxBits & WIFI_CONNECTED_BIT);

    dtlog_info(TAG, "station connected %s", (all_done ? "success" : "failure"));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// dtstation check if is connected
static bool
dtstation_is_connected(dtstation_espidf_t* this)
{
    return !!this->connection_failed;
}

// -----------------------------------------------------------------------------
// dtstation dispose function
static void
dtstation_dispose(dtstation_espidf_t* this)
{
    if (this == NULL)
        return;
    if (this->event_group_handle != NULL)
        vEventGroupDelete(this->event_group_handle);
    memset(this, 0, sizeof(*this));
}

// -----------------------------------------------------------------------------
// dtstation init function
dterr_t*
dtstation_init(dtstation_espidf_t* this)
{
    memset(this, 0, sizeof(*this));

    // init data members
    this->event_group_handle = xEventGroupCreate();
    if (this->event_group_handle == NULL)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to create event group");
    }

    this->retry_count = 0;
    this->connection_failed = false;

    // init method members
    this->connect = dtstation_connect;
    this->configure = dtstation_configure;
    this->is_connected = dtstation_is_connected;
    this->dispose = dtstation_dispose;

    return NULL;
}