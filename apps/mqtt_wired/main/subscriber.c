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

#include "subscriber.h"

#define TAG "subscriber"

// -------------------------------------------------------------------------------
// tasks's operating variables
typedef struct subscriber_t
{
    subscriber_config_t config;
    dtmqttclient_subscription_handle subscription_handle;
    bool _is_malloced;
} subscriber_t;

// -------------------------------------------------------------------------------
dterr_t*
subscriber_create(subscriber_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (subscriber_t*)malloc(sizeof(subscriber_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "%s failed to allocate %zu bytes for subscriber_t", __func__, sizeof(subscriber_t));
        goto cleanup;
    }

    DTERR_C(subscriber_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to create the subscriber task");
    }
    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
subscriber_init(subscriber_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to initialize subscriber task");

        subscriber_dispose(self);
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
subscriber_configure(subscriber_t* self, const subscriber_config_t* config)
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
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "failed to configure subscriber task");
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
subscriber_loop(subscriber_t* self)
{
    dterr_t* dterr = NULL;

    int loop = 0;
    while (true)
    {
        dtruntime_milliseconds_t now = dtruntime_now_milliseconds();

        dtbuffer_t* buffer = NULL;
        DTERR_C(dtmqttclient_receive(self->config.mqttclient_handle, self->subscription_handle, &buffer));

        dtlog_info(
          TAG, "subscriber_loop %d at %" DTRUNTIME_MILLISECONDS_PRI " ms got \"%s\"", loop, now, (char*)buffer->payload);

        dtbuffer_dispose(buffer);

        loop++;
    }

    goto cleanup;

cleanup:

    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "subscriber looping quit because of error");
    }
    else
    {
        dtlog_info(TAG, "subscriber_loop finished successfully");
    }

    return dterr;
}

// -------------------------------------------------------------------------------
dterr_t*
subscriber_tasker_entrypoint(void* self_arg, dttasker_handle tasker_handle)
{
    dterr_t* dterr = NULL;

    subscriber_t* self = (subscriber_t*)self_arg;

    DTERR_C(dtmqttclient_connect(self->config.mqttclient_handle));

    dtlog_info(TAG, "subscriber task has connected to MQTT broker");

    int32_t timeout_ms = 5000; // 5 seconds
    DTERR_C(dtmqttclient_subscribe(self->config.mqttclient_handle, self->config.topic, timeout_ms, &self->subscription_handle));

    dtlog_info(TAG, "subscriber task has subscribed to topic \"%s\"", self->config.topic);

    DTERR_C(dttasker_ready(tasker_handle));

    DTERR_C(subscriber_loop(self));

cleanup:
    subscriber_dispose(self);

    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "subscriber task failed and is quitting");
        dterr_each(dterr, dtmc_espidf_each_error_log, TAG);
    }

    return dterr;
}

// -------------------------------------------------------------------------------
void
subscriber_dispose(subscriber_t* self)
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
