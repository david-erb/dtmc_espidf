#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_chip_info.h>
#include <esp_err.h>
#include <esp_heap_caps.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtmqttclient.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtmqttclient_esp.h>

#include <dtmc/dtmc_espidf.h>

#include <dtmc/dtprepper_espidf.h>

#include "publisher.h"
#include "subscriber.h"

#define TAG "app_main"

#define TOPIC "dtmc_espidf/apps/mqtt_wired"

// -------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtprepper_espidf_t _prepper = { 0 }, *prepper = &_prepper;

    EventGroupHandle_t event_group = NULL;

    subscriber_config_t subscriber_config = { 0 };
    subscriber_t* subscriber = NULL;
    publisher_config_t publisher_config = { 0 };
    publisher_t* publisher = NULL;

    dttasker_espidf_config_t subscriber_tasker_config = { 0 };
    dttasker_espidf_t* subscriber_tasker = NULL;
    dttasker_espidf_config_t publisher_tasker_config = { 0 };
    dttasker_espidf_t* publisher_tasker = NULL;

    DTERR_C(dtmc_espidf_printf_environment());

    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);

    // -----------------------------------------------------------------

    DTERR_C(dtprepper_espidf_init(prepper));

    DTERR_C(prepper->configure(prepper));

    // normally we get radioconfig from NVS, but for testing we can set it here
    prepper->radioconfig.mqtt_host = "mqtt://192.168.0.157";
    prepper->radioconfig.mqtt_port = 1883;
    prepper->radioconfig.self_node_name = "Node1";

    DTERR_C(prepper->start_system(prepper));

    DTERR_C(prepper->start_ethernet(prepper));
    dtlog_info(TAG, "internet started");

    dtlog_info(TAG, "-----------------------------------------------------------------");
    dtlog_info(TAG, "free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    dtlog_info(TAG, "largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    DTERR_C(dtmc_espidf_printf_tasks());

    dtlog_info(TAG, "-----------------------------------------------------------------");

    // -----------------------------------------------------------------

    event_group = xEventGroupCreate();
    if (event_group == NULL)
    {
        dterr = dterr_new(DTERR_FAIL, __LINE__, __FILE__, __func__, NULL, "failed to create event group");
        goto cleanup;
    }

    // -----------------------------------------------------
    // configure the subscriber task

    {
        // private mqttclient handle for the subscriber task
        dtmqttclient_esp_t* mqttclient = NULL;
        DTERR_C(dtmqttclient_esp_create(&mqttclient));
        subscriber_config.mqttclient_handle = (dtmqttclient_handle)mqttclient;

        DTERR_C(dtmqttclient_esp_init(mqttclient));

        dtmqttclient_esp_config_t mqttclient_config = {
            .radioconfig = &prepper->radioconfig,
            .retry_count_maximum = 5,
        };
        DTERR_C(dtmqttclient_esp_configure(mqttclient, &mqttclient_config));
    }

    subscriber_config.topic = TOPIC;

    DTERR_C(subscriber_create(&subscriber));
    DTERR_C(subscriber_configure(subscriber, &subscriber_config));

    subscriber_tasker_config.name = "subscriber";                                  // name of the task
    subscriber_tasker_config.tasker_entry_point_fn = subscriber_tasker_entrypoint; // main function for the task
    subscriber_tasker_config.tasker_entry_point_arg = subscriber;                  // self pointer for the main function
    subscriber_tasker_config.stack_size = 4096;                                    // stack size for the task
    subscriber_tasker_config.priority = DTTASKER_PRIORITY_NORMAL_MEDIUM;           // priority of the task
    subscriber_tasker_config.core = 0;                                             // core affinity for the task
    DTERR_C(dttasker_espidf_create(&subscriber_tasker));
    DTERR_C(dttasker_espidf_configure(subscriber_tasker, &subscriber_tasker_config));

    // -----------------------------------------------------
    // configure the publisher task

    {
        // private mqttclient handle for the publisher task
        dtmqttclient_esp_t* mqttclient = NULL;
        DTERR_C(dtmqttclient_esp_create(&mqttclient));
        publisher_config.mqttclient_handle = (dtmqttclient_handle)mqttclient;

        DTERR_C(dtmqttclient_esp_init(mqttclient));

        dtmqttclient_esp_config_t mqttclient_config = {
            .radioconfig = &prepper->radioconfig,
            .retry_count_maximum = 5,
        };
        DTERR_C(dtmqttclient_esp_configure(mqttclient, &mqttclient_config));
    }

    publisher_config.topic = TOPIC;

    DTERR_C(publisher_create(&publisher));
    DTERR_C(publisher_configure(publisher, &publisher_config));

    publisher_tasker_config.name = "publisher";                                  // name of the task
    publisher_tasker_config.tasker_entry_point_fn = publisher_tasker_entrypoint; // main function for the task
    publisher_tasker_config.tasker_entry_point_arg = publisher;                  // self pointer for the main function
    publisher_tasker_config.stack_size = 4096;                                   // stack size for the task
    publisher_tasker_config.priority = DTTASKER_PRIORITY_NORMAL_MEDIUM;          // priority of the task
    publisher_tasker_config.core = 0;                                            // core affinity for the task
    DTERR_C(dttasker_espidf_create(&publisher_tasker));
    DTERR_C(dttasker_espidf_configure(publisher_tasker, &publisher_tasker_config));

    // -----------------------------------------------------
    DTERR_C(dttasker_espidf_start(subscriber_tasker));
    DTERR_C(dttasker_espidf_start(publisher_tasker));
    // -----------------------------------------------------
    while (true)
    {
        vTaskDelay(portMAX_DELAY); // Sleep indefinitely
    }

cleanup:
    if (dterr != NULL)
    {
        dterr_each(dterr, dtmc_espidf_each_error_log, TAG);
        dterr_dispose(dterr);
    }

    dtmqttclient_dispose(subscriber_config.mqttclient_handle);

    while (true)
    {
        vTaskDelay(portMAX_DELAY); // Sleep indefinitely
    }
}
