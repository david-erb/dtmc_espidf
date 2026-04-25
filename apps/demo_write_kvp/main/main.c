#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtkvp.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtnvblob.h>

// this concrete object is platform specific
#include <dtmc/dtnvblob_espidf_nvs.h>

#include <dtmc_base_demos/demo_write_kvp.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtkvp_list_t _demo_kvp_list = { 0 }, *kvp_list = &_demo_kvp_list;
    dtnvblob_handle nvblob_handle = NULL;
    demo_t* demo = NULL;

    // the kvp list we want to write
    {
        DTERR_C(dtkvp_list_init(kvp_list));

        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_SIGNATURE, "dtradioconfig_t mark 1"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_SELF_NODE_NAME, "RadiconfigNode"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_WIFI_SSID, "MyWiFiSSID"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_WIFI_PASSWORD, "MyWiFiPassword"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_MQTT_HOST, "mqtt://broker.example.com"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_MQTT_PORT, "1883"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_MQTT_WSPORT, "9001"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_MQTT_USER, "mqttuser"));
        DTERR_C(dtkvp_list_set(kvp_list, DTMC_BASE_CONSTANTS_KVP_KEY_MQTT_PASSWORD, "mqttpassword"));
    }

    // the nvblob destination
    {
        dtnvblob_espidf_nvs_t* o;
        DTERR_C(dtnvblob_espidf_nvs_create(&o));
        nvblob_handle = (dtnvblob_handle)o;
        dtnvblob_espidf_nvs_config_t c = { 0 };
        c.nvs_namespace = "default";
        c.key = "kvp_data";
        DTERR_C(dtnvblob_espidf_nvs_configure(o, &c));
    }

    // the demo instance which will do the writing
    {
        DTERR_C(dtmc_base_demo_write_kvp_create(&demo));
        dtmc_base_demo_write_kvp_config_t config = { 0 };
        // give it the kvp list to write
        config.kvp_list = kvp_list;
        // give it the nvblob handle
        config.nvblob_handle = nvblob_handle;
        DTERR_C(dtmc_base_demo_write_kvp_configure(demo, &config));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

    dtlog_info(TAG, "write to nvblob completed successfully");

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the objects
    dtnvblob_dispose(nvblob_handle);
    dtkvp_list_dispose(kvp_list);

    // Wait indefinitely to prevent the program from exiting.
    vTaskDelay(portMAX_DELAY);
}
