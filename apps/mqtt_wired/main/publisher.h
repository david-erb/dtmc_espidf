#include <dtmc_base/dttasker.h>

#define publisher_STACK_SIZE 4096
#define publisher_PRIORITY 5
#define publisher_CORE 1
#define publisher_ACTIVATED_BIT BIT0

typedef struct publisher_config_t
{
    dtmqttclient_handle mqttclient_handle;
    const char* topic;
} publisher_config_t;

typedef struct publisher_t publisher_t;

extern dterr_t*
publisher_create(publisher_t** self_ptr);

extern dterr_t*
publisher_init(publisher_t* self);

extern dterr_t*
publisher_configure(publisher_t* self, const publisher_config_t* config);

extern dterr_t*
publisher_tasker_entrypoint(void* self_arg, dttasker_handle tasker_handle);

extern dterr_t*
publisher_loop(publisher_t* self);

extern void
publisher_dispose(publisher_t* self);