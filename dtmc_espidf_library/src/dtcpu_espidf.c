#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_mac.h>
#include <esp_random.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtcpu.h>

// -----------------------------------------------------------------------------
dterr_t*
dtcpu_sysinit(void)
{
    return NULL;
}

// -----------------------------------------------------------------------------
void
dtcpu_mark(dtcpu_t* m)
{
    // caller promises: called once before and once after the hot section
    m->old = m->new;
    // ESP-IDF provides microseconds directly
    m->new = (uint64_t)esp_timer_get_time();
}

// -----------------------------------------------------------------------------
uint64_t
dtcpu_elapsed_microseconds(const dtcpu_t* m)
{
    return m->new - m->old;
}

// -----------------------------------------------------------------------------
void
dtcpu_busywait_microseconds(uint64_t microseconds)
{
    esp_rom_delay_us(microseconds);
}

// -----------------------------------------------------------------------------
// return permanent unique identifier string for this particular CPU/platform
const char*
dtcpu_identify(void)
{
    // Enough for "esp32-" + 12 hex chars + null
    static char ident[24] = { 0 };
    static bool initialized = false;

    if (!initialized)
    {
        uint8_t mac[6] = { 0 };
        esp_err_t err = esp_efuse_mac_get_default(mac);

        if (err != ESP_OK)
        {
            // Fallback if MAC cannot be read for some reason
            snprintf(ident, sizeof(ident), "esp32-UNKNOWN");
        }
        else
        {
            // Use factory MAC as stable unique ID
            // Example: "esp32-024A1BFFEE12"
            snprintf(ident, sizeof(ident), "esp32-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        initialized = true;
    }

    return ident;
}

// -----------------------------------------------------------------------------
int32_t
dtcpu_random_int32(void)
{
    return (int32_t)esp_random();
}