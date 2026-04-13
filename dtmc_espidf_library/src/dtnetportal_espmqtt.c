#include <stdlib.h>
#include <string.h>

#include <string.h>

#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtradioconfig.h>

#include <dtmc/dtmc_espidf.h>

#include <dtmc/dtnetportal_espmqtt.h>

DTNETPORTAL_INIT_VTABLE(dtnetportal_espmqtt);
DTOBJECT_INIT_VTABLE(dtnetportal_espmqtt);

static dterr_t*
dtnetportal_espmqtt__handle_mqtt_event_data(dtnetportal_espmqtt_t* self,
  const char* topic,
  int topic_len,
  const char* data,
  int data_len);

#define EVENT_CONNECTED_BIT BIT0
#define EVENT_ERROR_BIT BIT1
#define EVENT_SUBSCRIBED_BIT BIT2

#define LOG_ERROR_IF_NONZERO(message, error_code)                                                                              \
    if (error_code != 0)                                                                                                       \
    {                                                                                                                          \
        dtlog_error(TAG, "last error %s: 0x%x", message, error_code);                                                          \
    }

typedef struct dtnetportal_espmqtt_t
{
    DTNETPORTAL_COMMON_MEMBERS;

    // data members
    dtnetportal_espmqtt_config_t config;

    dtsemaphore_handle manifold_semaphore;
    dtmanifold_t _manifold, *manifold;

    esp_mqtt_client_handle_t esp_mqtt_client_handle;
    EventGroupHandle_t event_group_handle;
    bool connection_failed;
    bool _is_connected; // Indicates if the client is connected to the MQTT broker.
    bool _is_malloced;

} dtnetportal_espmqtt_t;

#define TAG "dtnetportal_espmqtt"
#define dtlog_debug(...)

// -----------------------------------------------------------------------------
static void
mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    dterr_t* dterr = NULL;
    dtnetportal_espmqtt_t* self = (dtnetportal_espmqtt_t*)handler_args;

    // ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            dtlog_debug(TAG, "MQTT_EVENT_CONNECTED on 0x%p", client);
            xEventGroupSetBits(self->event_group_handle, EVENT_CONNECTED_BIT);
            break;

        case MQTT_EVENT_DISCONNECTED:
            // TODO: dtnetportal_espmqtt_t should gracefully handle disconnection including unsubscribing
            dtlog_debug(TAG, "MQTT_EVENT_DISCONNECTED on 0x%p", client);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            dtlog_debug(TAG, "MQTT_EVENT_SUBSCRIBED mid=%d on 0x%p", event->msg_id, client);

            xEventGroupSetBits(self->event_group_handle, EVENT_SUBSCRIBED_BIT);

            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            // TODO: dtnetportal_espmqtt_t should handle unsubscribing from topics
            break;
        case MQTT_EVENT_PUBLISHED:
            printf("MQTT_EVENT_PUBLISHED, msg_id=%d\n", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            DTERR_C(
              dtnetportal_espmqtt__handle_mqtt_event_data(self, event->topic, event->topic_len, event->data, event->data_len));
            break;
        case MQTT_EVENT_ERROR:
            dtlog_error(TAG, "MQTT_EVENT_ERROR type %d on 0x%p", event->error_handle->error_type, client);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                LOG_ERROR_IF_NONZERO("    reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                LOG_ERROR_IF_NONZERO("    reported from tls stack", event->error_handle->esp_tls_stack_err);
                LOG_ERROR_IF_NONZERO("    captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                if (event->error_handle->esp_transport_sock_errno != 0)
                {
                    dtlog_error(TAG,
                      "    last errno %d string: %s",
                      event->error_handle->esp_transport_sock_errno,
                      strerror(event->error_handle->esp_transport_sock_errno));
                }
            }
            dtlog_error(TAG, "setting EVENT_ERROR_BIT in event group 0x%p on 0x%p", self->event_group_handle, client);
            xEventGroupSetBits(self->event_group_handle, EVENT_ERROR_BIT);
            break;
        default:
            dtlog_debug(TAG, "Other event id:%d on 0x%p", event->event_id, client);
            break;
    }

cleanup:
    if (dterr)
    {
        dtmc_espidf_each_error_log(dterr, NULL);
        dterr_dispose(dterr);
    }
}

// --------------------------------------------------------------------------------------
extern dterr_t*
dtnetportal_espmqtt_create(dtnetportal_espmqtt_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtnetportal_espmqtt_t*)malloc(sizeof(dtnetportal_espmqtt_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM,
          DTERR_LOC,
          NULL,
          "failed to allocate %zu bytes for dtnetportal_espmqtt_t",
          sizeof(dtnetportal_espmqtt_t));
        goto cleanup;
    }

    DTERR_C(dtnetportal_espmqtt_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtnetportal_espmqtt_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_configure(dtnetportal_espmqtt_t* self, dtnetportal_espmqtt_config_t* config)
{
    dterr_t* dterr = NULL;
    if (self == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "self is NULL");
        goto cleanup;
    }
    if (config->radioconfig == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "radioconfig is NULL");
        goto cleanup;
    }
    if (config->radioconfig->mqtt_host == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "radioconfig mqtt_host is NULL");
        goto cleanup;
    }
    if (config->radioconfig->mqtt_port == 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "radioconfig mqtt_port is 0");
        goto cleanup;
    }

    self->config = *config;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, dterr, "unable to complete mqtt configuration");
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_init(dtnetportal_espmqtt_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_NETPORTAL_MODEL_MQTT_ESPIDF;

    // set the vtable for this model number
    DTERR_C(dtnetportal_set_vtable(self->model_number, &dtnetportal_espmqtt_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtnetportal_espmqtt_object_vt));

    // init data members
    self->event_group_handle = xEventGroupCreate();
    if (self->event_group_handle == NULL)
    {
        dterr = dterr_new(DTERR_FAIL, __LINE__, __FILE__, __func__, NULL, "failed to create event group");
        goto cleanup;
    }

    self->manifold = &self->_manifold;
    DTERR_C(dtmanifold_init(self->manifold));

    // create a semaphore for the manifold so it self-serializes
    {
        DTERR_C(dtsemaphore_create(&self->manifold_semaphore, 1, 1));
        DTERR_C(dtmanifold_set_threadsafe_semaphore(self->manifold, self->manifold_semaphore, 10));
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtnetportal_espmqtt_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_activate(dtnetportal_espmqtt_t* self DTNETPORTAL_ACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = self->config.radioconfig->mqtt_host,
        .broker.address.port = self->config.radioconfig->mqtt_port,
        // TODO: Assign proper dtnetportal_espmqtt_t credentials.client_id.
        .credentials.set_null_client_id = true,
        // .buffer.out_size = 4096,
    };

    self->esp_mqtt_client_handle = esp_mqtt_client_init(&mqtt_cfg);

    if (self->esp_mqtt_client_handle == NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to init the ESP mqtt client");
        goto cleanup;
    }

    /* The last argument may be used to pass data to the event handler, in self example mqtt_event_handler */
    DTMC_ESPIDF_C(esp_mqtt_client_register_event(self->esp_mqtt_client_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, self));

    dtlog_debug(TAG, "starting ESP mqtt task on 0x%p", self->esp_mqtt_client_handle);
    DTMC_ESPIDF_C(esp_mqtt_client_start(self->esp_mqtt_client_handle));

    // Wait for subscription to complete, will be instant if already subscribed.
    EventBits_t uxBits = xEventGroupWaitBits(self->event_group_handle, // The event group handle.
      EVENT_CONNECTED_BIT,                                             // Bit to wait for.
      pdFALSE,                                                         // Do not clear the bit on exit.
      pdFALSE,                                                         // Wait for only (the) one bit.
      pdMS_TO_TICKS(10000));

    bool all_done = (uxBits & EVENT_CONNECTED_BIT) && !(uxBits & EVENT_ERROR_BIT);

    dtlog_debug(TAG, "mqttclient connected %s on 0x%p", (all_done ? "success" : "failure"), self->esp_mqtt_client_handle);

    if (!all_done)
    {
        dterr = dterr_new(DTERR_IO,
          DTERR_LOC,
          NULL,
          "timeout waiting for connection to %s:%ld",
          self->config.radioconfig->mqtt_host,
          self->config.radioconfig->mqtt_port);
        goto cleanup;
    }

    self->_is_connected = true;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to connect to mqtt broker");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_subscribe(dtnetportal_espmqtt_t* self DTNETPORTAL_SUBSCRIBE_ARGS)
{
    // TODO: dtnetportal_espmqtt_t should check if connected before attempting to subscribe.

    dterr_t* dterr = NULL;

    if (!self->_is_connected ||                          //
        topic == NULL ||                                 //
        topic[0] == '\0' ||                              //
        strlen(topic) >= DTNETPORTAL_MAX_TOPIC_LENGTH || //
        recipient_self == NULL || receive_callback == NULL)
    {
        dterr = dterr_new(DTERR_ARGUMENT_NULL,
          DTERR_LOC,
          NULL,
          "NULL argument among self=%p, topic=%p, topic_length=%zu, recipient_self=%p, receive_callback=%p",
          self,
          topic,
          topic ? strlen(topic) : 0,
          recipient_self,
          receive_callback);
        goto cleanup;
    }

    dtlog_debug(TAG, "subscribing to topic \"%s\"", topic);

    // 1) Always register with manifold
    DTERR_C(dtmanifold_subscribe(self->manifold, topic, recipient_self, receive_callback));

    // 2) Register with the broker. This returns instantly if already subscribed.
    esp_mqtt_client_subscribe(self->esp_mqtt_client_handle, topic, 0);

    // Wait for subscription to complete, will be instant if already subscribed.
    EventBits_t uxBits = xEventGroupWaitBits(self->event_group_handle, // The event group handle.
      EVENT_SUBSCRIBED_BIT,                                            // Bit to wait for.
      pdFALSE,                                                         // Do not clear the bit on exit.
      pdFALSE,                                                         // Wait for only (the) one bit.
      pdMS_TO_TICKS(10000));

    bool is_subscribed = (uxBits & EVENT_SUBSCRIBED_BIT);

    if (!is_subscribed)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to subscribe to topic \"%s\"", topic ? topic : "NULL");
        goto cleanup;
    }

cleanup:

    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to subscribe to topic \"%s\"", topic ? topic : "NULL");

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_publish(dtnetportal_espmqtt_t* self DTNETPORTAL_PUBLISH_ARGS)
{
    dterr_t* dterr = NULL;

    if (!self->_is_connected)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "not connected");
    }

    // printf("publishing to topic \"%s\"\n", topic);
    // printf("  buffer is 0x%p\n", buffer);
    // printf("  buffer flags are 0x%08lx\n", buffer->flags);
    // printf("  buffer length is %ld\n", buffer->length);
    // printf("  buffer payload is 0x%p\n", buffer->payload);
    // printf("  buffer payload as string is \"%s\"\n", (char*)buffer->payload);

    // chatgpt says: self blocks until the message is safely copied into the MQTT client's internal buffer
    int msg_id = esp_mqtt_client_publish(self->esp_mqtt_client_handle, topic, buffer->payload, buffer->length, 0, 0);

    // When I used self for 200-byte messages, it seemed only to enqueue about 1 message per second.
    // Maybe it was the default buffer space not big enough?
    // int msg_id = esp_mqtt_client_enqueue(self->esp_mqtt_client_handle,
    //   topic,
    //   buffer->payload,
    //   buffer->length,
    //   0,     // qos
    //   0,     // retain
    //   true); // store

    if (msg_id == -1)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to publish message to topic \"%s\"", topic);
        goto cleanup;
    }

    // printf("* published message to topic \"%s\" with msg_id %d\n", topic, msg_id);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_espmqtt_get_info(dtnetportal_espmqtt_t* self, dtnetportal_info_t* info)
{
    dterr_t* dterr = NULL;
    if (info == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "called with NULL info");
        goto cleanup;
    }

    memset(info, 0, sizeof(*info));

    info->flavor = DTNETPORTAL_ESPMQTT_FLAVOR;
    info->version = DTNETPORTAL_ESPMQTT_VERSION;

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to obtain netportal info");

    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtnetportal_espmqtt_dispose(dtnetportal_espmqtt_t* self)
{
    if (self == NULL)
        return;

    if (self->esp_mqtt_client_handle != NULL)
    {
        esp_mqtt_client_stop(self->esp_mqtt_client_handle);
        esp_mqtt_client_destroy(self->esp_mqtt_client_handle);
    }

    if (self->event_group_handle != NULL)
        vEventGroupDelete(self->event_group_handle);

    if (self->_is_malloced)
    {
        free(self);
    }
    else
    {
        memset(self, 0, sizeof(*self));
    }
}

// ----------------------------------------------------------------
static dterr_t*
dtnetportal_espmqtt__handle_mqtt_event_data( //
  dtnetportal_espmqtt_t* self,
  const char* topic,
  int topic_len,
  const char* data,
  int data_len)
{
    dterr_t* dterr = NULL;
    dtbuffer_t* buffer = NULL;

    if (!self || !self->manifold || !topic || topic_len > DTNETPORTAL_MAX_TOPIC_LENGTH || !data || data_len <= 0)
    {
        dterr = dterr_new(DTERR_BADARG,
          DTERR_LOC,
          NULL,
          "invalid arguments among self 0x%p, manifold 0x%p, topic 0x%p, topic_len %d, data 0x%p, data_len %d",
          self,
          (self ? self->manifold : NULL),
          topic,
          topic_len,
          data,
          data_len);
        goto cleanup;
    }

    char topic_string[DTNETPORTAL_MAX_TOPIC_LENGTH + 1];
    memcpy(topic_string, topic, topic_len);
    topic_string[topic_len] = '\0';

    dtlog_debug(TAG, "%s(): received %d bytes on topic \"%s\"", __func__, data_len, topic_string);

    // wrap a buffer around the data that was sent by the device (not copying it here)
    DTERR_C(dtbuffer_create(&buffer, 0));
    // substitute for the payload
    buffer->payload = (void*)data;
    buffer->length = data_len;

    // fan-out to local recipients via manifold
    dtlog_debug(TAG, "%s(): forwarding %d bytes to manifold on topic \"%s\"", __func__, data_len, topic_string);
    DTERR_C(dtmanifold_publish(self->manifold, topic_string, buffer));

cleanup:
    dtbuffer_dispose(buffer);

    if (dterr != NULL)
    {
        dterr = dterr_new(dterr->error_code,
          DTERR_LOC,
          dterr,
          "unable to publish received mqtt message to manifold for topic \"%s\"",
          topic_string);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtnetportal_espmqtt_copy(dtnetportal_espmqtt_t* this, dtnetportal_espmqtt_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtnetportal_espmqtt_equals(dtnetportal_espmqtt_t* a, dtnetportal_espmqtt_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtnetportal_espmqtt_equals backend.
    return (a->model_number == b->model_number);
}

// --------------------------------------------------------------------------------------------
const char*
dtnetportal_espmqtt_get_class(dtnetportal_espmqtt_t* self)
{
    return "dtnetportal_espmqtt_t";
}

// --------------------------------------------------------------------------------------------

bool
dtnetportal_espmqtt_is_iface(dtnetportal_espmqtt_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTNETPORTAL_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtnetportal_espmqtt_to_string(dtnetportal_espmqtt_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    strncpy(buffer, "dtnetportal_espmqtt_t", buffer_size);
    buffer[buffer_size - 1] = '\0';
}
