#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_chip_info.h>
#include <esp_timer.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>
#include <dtmc_base/dttasker_registry.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc/dtmc_espidf.h>

// CONFIG_* is 1 or undefined; IS_ENABLED() -> 1 or 0
/* --- Generic IS_ENABLED() for ESP-IDF (1 or undefined) --- */
#define __ARG_PLACEHOLDER_1 0,
#define __TAKE_SECOND_ARG(_ignored, val, ...) val
#define __IS_DEFINED(x) ___IS_DEFINED(x)
#define ___IS_DEFINED(val) ____IS_DEFINED(__ARG_PLACEHOLDER_##val)
#define ____IS_DEFINED(arg1_or_junk) __TAKE_SECOND_ARG(arg1_or_junk 1, 0)

/* Returns 1 if cfg macro is defined as 1, else 0 */
#define IS_ENABLED(cfg) __IS_DEFINED(cfg)
#define APPEND_CONFIG_FLAG(SYM)                                                                                                \
    do                                                                                                                         \
    {                                                                                                                          \
        const char* v = IS_ENABLED(SYM) ? "y" : "n";                                                                           \
        s = dtstr_concat_format(s, t, item_format_str, p, #SYM, v);                                                            \
    } while (0)

#define TAG "dtruntime_espidf"

// --------------------------------------------------------------------------------------
const char*
dtruntime_flavor(void)
{
    return DTMC_ESPIDF_FLAVOR;
}

// --------------------------------------------------------------------------------------
const char*
dtruntime_version(void)
{
    return DTMC_ESPIDF_VERSION;
}

// --------------------------------------------------------------------------------------
bool
dtruntime_is_qemu()
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.revision == 300;
}

// --------------------------------------------------------------------------------------
extern dtruntime_milliseconds_t
dtruntime_now_milliseconds()
{
    return (dtruntime_milliseconds_t)(esp_timer_get_time() / 1000); // Prefer ESP-IDF's timer when available
}

// --------------------------------------------------------------------------------------
extern void
dtruntime_sleep_milliseconds(dtruntime_milliseconds_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

// --------------------------------------------------------------------------------------
dterr_t*
dtruntime_format_environment_as_table(char** out_string)
{
    dterr_t* dterr = NULL;
    *out_string = NULL;

    char* s = NULL;
    char* t = "\n";
    char* p = "    ";

    const char* item_format_str = "%s%-48s %-24s";
    const char* item_format_int = "%s%-48s %" PRIu64;
    const char* item_format_double = "%s%-48s %g";

    s = dtstr_concat_format(s, t, item_format_str, p, "Flavor", dtruntime_flavor());
    s = dtstr_concat_format(s, t, item_format_str, p, "Version", dtruntime_version());

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    const char* chip_model;
    if (chip_info.model == CHIP_ESP32)
    {
        chip_model = "ESP32";
    }
    else if (chip_info.model == CHIP_ESP32S2)
    {
        chip_model = "ESP32S2";
    }
    else if (chip_info.model == CHIP_ESP32S3)
    {
        chip_model = "ESP32S3";
    }
    else if (chip_info.model == CHIP_ESP32C3)
    {
        chip_model = "ESP32C3";
    }
    else if (chip_info.model == CHIP_ESP32C6)
    {
        chip_model = "ESP32C6";
    }
    else if (chip_info.model == CHIP_POSIX_LINUX)
    {
        chip_model = "POSIX_LINUX";
    }
    else
    {
        chip_model = "Unknown";
    }

    s = dtstr_concat_format(s, t, item_format_str, p, "Chip model", chip_model);

    s = dtstr_concat_format(s, t, item_format_int, p, "Chip Revision", (uint64_t)chip_info.revision);
    s = dtstr_concat_format(s, t, item_format_int, p, "Chip Cores", (uint64_t)chip_info.cores);

    if (chip_info.features & CHIP_FEATURE_EMB_FLASH)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip Embedded Flash", "Yes");
    }
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip WIFI", "Yes");
    }
    if (chip_info.features & CHIP_FEATURE_BLE)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip BLE", "Yes");
    }
    if (chip_info.features & CHIP_FEATURE_BT)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip BT", "Yes");
    }
    if (chip_info.features & CHIP_FEATURE_IEEE802154)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip IEEE802154", "Yes");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM)
    {
        s = dtstr_concat_format(s, t, item_format_str, p, "Chip Embedded PSRAM", "Yes");
    }

    s = dtstr_concat_format(s, t, item_format_str, p, "Is QEMU", dtruntime_is_qemu() ? "Yes" : "No");
    s = dtstr_concat_format(s, t, item_format_int, p, "configTICK_RATE_HZ", (uint64_t)configTICK_RATE_HZ);
    s = dtstr_concat_format(s, t, item_format_double, p, "frequency", (double)(1000.0 / portTICK_PERIOD_MS));

    APPEND_CONFIG_FLAG(CONFIG_FREERTOS_USE_TRACE_FACILITY);
    APPEND_CONFIG_FLAG(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS);
    APPEND_CONFIG_FLAG(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER);
    *out_string = s;

    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
noop_tasker_entry_point_fn(void* arg, dttasker_handle handle)
{
    (void)arg;
    (void)handle;
    return NULL;
}

// --------------------------------------------------------------------------------------

static void
measure_cpu_usage(TaskHandle_t task, dttasker_info_t* info)
{
    if (!info)
        return;

    // 1) Read cumulative run-time for the task
    TaskStatus_t ts = { 0 };
    vTaskGetInfo(task, &ts, pdTRUE /*include run-time stats*/, eInvalid);

    // When CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y, ulRunTimeCounter is in microseconds.
    uint64_t used_us = (uint64_t)ts.ulRunTimeCounter;

    // 2) Read wall time in microseconds
    int64_t now_us = esp_timer_get_time();

    // 3) Compute deltas
    int64_t dt_us = now_us - info->time_microseconds;
    int64_t du_us = (int64_t)(used_us - info->used_microseconds);

    // 4) Stash for next time
    info->time_microseconds = now_us;
    info->used_microseconds = used_us;

    // 5) Derive %
    if (now_us == 0)
    {
        info->cpu_percent_used = -2; // not initialized yet
    }
    else if (dt_us <= 0)
    {
        info->cpu_percent_used = -1; // no time elapsed (or clock anomaly)
    }
    else
    {
        // Same math as your Zephyr version: percent over the interval
        info->cpu_percent_used = (int)((100LL * du_us) / dt_us);
    }
}

// --------------------------------------------------------------------------------------

// callback invoked for each thread in the system
static void
thread_report_cb(TaskStatus_t* task_status, dttasker_registry_t* registry)
{
    dterr_t* dterr = NULL;

    BaseType_t core = task_status->xCoreID;
    if (core == tskNO_AFFINITY)
    {
        core = -1;
    }
    UBaseType_t priority = task_status->uxCurrentPriority;
    const char* thread_name = task_status->pcTaskName;

    if (thread_name == NULL || strlen(thread_name) == 0)
    {
        thread_name = "unnamed";
    }

    dtguid_t guid;
    dtguid_generate_from_string(&guid, thread_name);
    char guid_str[37];
    dtguid_to_string(&guid, guid_str, sizeof(guid_str));

    dttasker_handle tasker_handle = NULL;
    DTERR_C(dtguidable_pool_search(&registry->pool, &guid, (dtguidable_handle*)&tasker_handle));
    if (tasker_handle != NULL)
    {
        // dtlog_debug(TAG, "%s(): updating thread task \"%s\" (%s) already in registry", __func__, thread_name, guid_str);
    }
    else
    {
        // dtlog_debug(TAG, "%s(): creating thread task \"%s\" (%s) not yet in registry", __func__, thread_name, guid_str);
        dttasker_config_t c = { 0 };
        c.name = thread_name;
        c.tasker_entry_point_fn = noop_tasker_entry_point_fn;
        c.stack_size = 1;
        // creaete a dummy task object to represent this thread
        DTERR_C(dttasker_create(&tasker_handle, &c));

        DTERR_C(dttasker_registry_insert(registry, tasker_handle));
    }

    // get the information on the task that we have been keeping track of
    dttasker_info_t info = { 0 };
    DTERR_C(dttasker_get_info(tasker_handle, &info));

    // keep the same thread name always
    strncpy(info._name, thread_name, sizeof(info._name) - 1);
    info._name[sizeof(info._name) - 1] = '\0';
    info.name = info._name;

    // presume it's running
    info.status = RUNNING;
    info.priority = (int)priority;
    info.core = (int)core;

    measure_cpu_usage(task_status->xHandle, &info);

    DTERR_C(dttasker_set_info(tasker_handle, &info));

cleanup:

    if (dterr != NULL)
        dtlog_error(TAG, "%s(): error processing thread \"%s\": %s", __func__, thread_name, dterr->message);
    return;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtruntime_register_tasks(dttasker_registry_t* registry)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(registry);

    // TODO: Add locking around registry auto-init in dtruntime_register_tasks().
    if (!registry->is_initialized)
    {
        DTERR_C(dttasker_registry_init(registry));
    }

    // force context switch so cpu accounting gets done
    vTaskDelay(0);

#define MAX_TASKS 32
    TaskStatus_t taskArray[MAX_TASKS];
    UBaseType_t numTasks;
    uint32_t totalRunTime;

    numTasks = uxTaskGetSystemState(taskArray, MAX_TASKS, &totalRunTime);
    for (UBaseType_t i = 0; i < numTasks; i++)
    {
        thread_report_cb(&taskArray[i], registry);
    }

    return NULL; // success

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtruntime_format_devices_as_table(char** out_string)
{
    dterr_t* dterr = NULL;
    *out_string = NULL;
    DTERR_ASSERT_NOT_NULL(out_string);

    char* p = "    ";
    char* s = NULL;
    char* t = "\n";

    s = dtstr_concat_format(s, t, "%sthe device list feature is not implemented in dtmc_espidf_library", p);

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        dtstr_dispose(s);
        s = NULL;
    }

    *out_string = s;

    return dterr;
}

// --------------------------------------------------------------------------------------------
void
dtruntime_log_devices(const char* tag, dtlog_level_t log_level, const char* prefix)
{
    dterr_t* dterr = NULL;
    char* s = NULL;

    dtruntime_format_devices_as_table(&s);

    if (dterr == NULL)
        dtlog_info(tag, "%s:\n%s", prefix, s);
    else
        dtlog_error(tag, "%s: failed to format runtime devices: %s", prefix, dterr->message);

    dtstr_dispose(s);
}
