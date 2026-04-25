#include <dtmc_base/dttasker.h>

#define subscriber_STACK_SIZE 4096
#define subscriber_PRIORITY 5
#define subscriber_CORE 1
#define subscriber_ACTIVATED_BIT BIT0

typedef struct subscriber_config_t
{
    dtmqttclient_handle mqttclient_handle;
    const char* topic;
} subscriber_config_t;

typedef struct subscriber_t subscriber_t;

extern dterr_t*
subscriber_create(subscriber_t** self_ptr);

extern dterr_t*
subscriber_init(subscriber_t* self);

extern dterr_t*
subscriber_configure(subscriber_t* self, const subscriber_config_t* config);

extern dterr_t*
subscriber_tasker_entrypoint(void* self_arg, dttasker_handle tasker_handle);

extern dterr_t*
subscriber_loop(subscriber_t* self);

extern void
subscriber_dispose(subscriber_t* self);