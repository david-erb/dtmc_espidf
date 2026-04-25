#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <esp_task_wdt.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtmqttclient.h>

#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtmc_espidf.h>
#include <dtmc/dtmqttclient_esp.h>

#include "publisher.h"

#define TAG "publisher"

// -------------------------------------------------------------------------------
// tasks's operating variables
typedef struct publisher_t
{
    publisher_config_t config;
    bool _is_malloced;
} publisher_t;

// -------------------------------------------------------------------------------
dterr_t*
publisher_create(publisher_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (publisher_t*)malloc(sizeof(publisher_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "%s failed to allocate %zu bytes for publisher_t", __func__, sizeof(publisher_t));
        goto cleanup;
    }

    DTERR_C(publisher_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to create the publisher task");
    }
    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
publisher_init(publisher_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to initialize publisher task");

        publisher_dispose(self);
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
publisher_configure(publisher_t* self, const publisher_config_t* config)
{
    dterr_t* dterr = NULL;

    if (self == NULL)
    {
        dterr = dterr_new(DTERR_ARGUMENT_NULL, DTERR_LOC, NULL, "self is NULL");
        goto cleanup;
    }

    if (config->topic == NULL || strlen(config->topic) == 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "topic is NULL or empty");
        goto cleanup;
    }

    self->config = *config;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to configure publisher task");
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
publisher_loop(publisher_t* self)
{
    dterr_t* dterr = NULL;
    dtbuffer_t _buffer = { 0 }, *buffer = &_buffer;
    char messsage[256];

    int loop = 0;

    while (true)
    {
        dtruntime_milliseconds_t now = dtruntime_now_milliseconds();

        sprintf(messsage, "publishing loop %d at %" DTRUNTIME_MILLISECONDS_PRI " ms", loop, now);

        DTERR_C(dtbuffer_wrap(buffer, messsage, strlen(messsage) + 1));

        DTERR_C(dtmqttclient_publish(self->config.mqttclient_handle, self->config.topic, buffer));

        vTaskDelay(pdMS_TO_TICKS(1000));

        loop++;
    }

cleanup:

    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "publisher looping quit because of error");
    }
    else
    {
        dtlog_info(TAG, "publisher_loop finished successfully");
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
publisher_tasker_entrypoint(void* self_arg, dttasker_handle tasker_handle)
{
    dterr_t* dterr = NULL;

    publisher_t* self = (publisher_t*)self_arg;

    DTERR_C(dtmqttclient_connect(self->config.mqttclient_handle));

    dtlog_info(TAG, "publisher task has connected to MQTT broker");

    DTERR_C(dttasker_ready(tasker_handle));

    DTERR_C(publisher_loop(self));

cleanup:
    publisher_dispose(self);

    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "publisher task failed and is quitting");
        dterr_each(dterr, dtmc_espidf_each_error_log, TAG);
    }

    return dterr;
}

// -------------------------------------------------------------------------------
void
publisher_dispose(publisher_t* self)
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
