#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_chip_info.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc/dtmc_espidf.h>

const char*
dtmc_espidf_version(void)
{
    return DTMC_ESPIDF_VERSION;
}

// --------------------------------------------------------------------------------------
void
dtmc_espidf_each_error_log(dterr_t* dterr, void* context)
{
    const char* tag = (const char*)context;
    dtlog_error(tag, "%s@%ld in %s: %s", dterr->source_file, (long)dterr->line_number, dterr->source_function, dterr->message);
}

// --------------------------------------------------------------------------------------
dterr_t*
dtmc_espidf_is_qemu(bool* is_qemu)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    *is_qemu = chip_info.revision == 300;
    return NULL; // success
}

// -------------------------------------------------------------------------------
dterr_t*
dtmc_espidf_printf_environment(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("*dtmc_espidf_printf_environment: Chip:\n");
    if (chip_info.model == CHIP_ESP32)
    {
        printf("*    model: ESP32\n");
    }
    else if (chip_info.model == CHIP_ESP32S2)
    {
        printf("*    model: ESP32S2\n");
    }
    else if (chip_info.model == CHIP_ESP32S3)
    {
        printf("*    model: ESP32S3\n");
    }
    else if (chip_info.model == CHIP_ESP32C3)
    {
        printf("*    model: ESP32C3\n");
    }
    else if (chip_info.model == CHIP_ESP32C6)
    {
        printf("*    model: ESP32C6\n");
    }
    else if (chip_info.model == CHIP_POSIX_LINUX)
    {
        printf("*    model: POSIX_LINUX\n");
    }
    else
    {
        printf("*    model: %d\n", chip_info.model);
    }

    printf("*    revision: %d\n", chip_info.revision);

    printf("*    cores: %d\n", chip_info.cores);

    if (chip_info.features & CHIP_FEATURE_EMB_FLASH)
    {
        printf("*    embedded flash\n");
    }
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN)
    {
        printf("*    WIFI\n");
    }
    if (chip_info.features & CHIP_FEATURE_BLE)
    {
        printf("*    BLE\n");
    }
    if (chip_info.features & CHIP_FEATURE_BT)
    {
        printf("*    BT\n");
    }
    if (chip_info.features & CHIP_FEATURE_IEEE802154)
    {
        printf("*    IEEE802154\n");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM)
    {
        printf("*    embedded PSRAM\n");
    }

    printf("*dtmc_espidf_printf_environment: Clock:\n");
    printf("*    configTICK_RATE_HZ is %d\n", configTICK_RATE_HZ);
    printf("*    frequency is %0.3f\n", (double)(1000.0 / portTICK_PERIOD_MS));

    return NULL; // success
}

// -------------------------------------------------------------------------------
dterr_t*
dtmc_espidf_printf_tasks(void)
{
#define MAX_TASKS 32
    TaskStatus_t taskArray[MAX_TASKS];
    UBaseType_t numTasks;
    uint32_t totalRunTime;

    numTasks = uxTaskGetSystemState(taskArray, MAX_TASKS, &totalRunTime);

    printf("*dtmc_espidf_printf_tasks: Tasks:\n");

#define HEAD_FORMAT "*        %-20s %s %-10s\n"
#define BODY_FORMAT "*    %3d. %-20s %2d   %4u\n"

    printf(HEAD_FORMAT, "Task Name", "Core", "Priority");
    for (UBaseType_t i = 0; i < numTasks; i++)
    {
        BaseType_t core = taskArray[i].xCoreID;
        if (core == tskNO_AFFINITY)
        {
            core = -1;
        }
        UBaseType_t priority = taskArray[i].uxCurrentPriority;
        printf(BODY_FORMAT, (int)i, taskArray[i].pcTaskName, core, priority);
    }

    if (numTasks > MAX_TASKS)
    {
        printf("*    WARNING: MAX_TASKS reached (%d left unreported)\n", numTasks - MAX_TASKS);
    }
    return NULL; // success
}