#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <lwip/err.h>
#include <lwip/sys.h>

#include <nvs_flash.h>

#include <esp_eth.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtradioconfig.h>

#include <dtmc/dtethernet_espidf.h>
#include <dtmc/dtmc_espidf.h>

#define TAG "dtethernet"

static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t* s_mac = NULL;
static esp_eth_phy_t* s_phy = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

#define ETHERNET_EMAC_TASK_STACK_SIZE 2048

// -----------------------------------------------------------------------------
// dtethernet configure function
static dterr_t*
dtethernet_configure(dtethernet_espidf_t* this, dtethernet_espidf_config_t* config)
{
    this->config = config;
    return NULL;
}

// -----------------------------------------------------------------------------

static void
eth_on_got_ip(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // dtethernet_espidf_t* this = (dtethernet_espidf_t*)arg;

    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    dtlog_info(TAG,
      "Got IPv4 event: Interface \"%s\" address: " IPSTR,
      esp_netif_get_desc(event->esp_netif),
      IP2STR(&event->ip_info.ip));
    xSemaphoreGive(s_semph_get_ip_addrs);
}

// -----------------------------------------------------------------------------
// dtethernet connection function
static dterr_t*
dtethernet_connect(dtethernet_espidf_t* this)
{
    dterr_t* dterr = NULL;

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    // Warning: the interface desc is used in tests to capture actual
    // connection details (IP, gw, mask)
    esp_netif_config.if_desc = "netif dtethernet";
    esp_netif_config.route_prio = 64;
    esp_netif_config_t netif_config = { .base = &esp_netif_config, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH };
    esp_netif_t* netif = esp_netif_new(&netif_config);
    if (!netif)
    {
        return dterr_new(DTERR_FAIL, __LINE__, __FILE__, __func__, NULL, "failed to create ethernet interface");
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = ETHERNET_EMAC_TASK_STACK_SIZE;
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.autonego_timeout_ms = 100;
    s_mac = esp_eth_mac_new_openeth(&mac_config);
    s_phy = esp_eth_phy_new_dp83848(&phy_config);

    // Install Ethernet driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    DTMC_ESPIDF_C(esp_eth_driver_install(&config, &s_eth_handle));

    uint8_t eth_mac[6] = { 0 };
    DTMC_ESPIDF_C(esp_read_mac(eth_mac, ESP_MAC_ETH));
    DTMC_ESPIDF_C(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac));

    // combine driver with netif
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    esp_netif_attach(netif, s_eth_glue);

    // Register user defined event handers
    DTMC_ESPIDF_C(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip, NULL));

    esp_eth_start(s_eth_handle);

    dtlog_info(TAG, "waiting for ethernet connection");

    int32_t timeout_ms = 1000000; // 1000 seconds
    bool timed_out = !xSemaphoreTake(s_semph_get_ip_addrs, pdMS_TO_TICKS(timeout_ms));

    this->connection_succeeded = !timed_out;

    if (this->connection_succeeded)
    {
        dtlog_info(TAG, "ethernet connection %s", (this->connection_succeeded ? "success" : "failure"));
    }
    else
    {
        dtlog_error(TAG, "ethernet connection timeout before %d ms", 1000000);
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// dtethernet wait to be connected
static bool
dtethernet_connected_waitfunc(void* object, void* context, int32_t timeout_ms)
{
    dtethernet_espidf_t* this = (dtethernet_espidf_t*)object;

    bool timed_out = !xSemaphoreTake(s_semph_get_ip_addrs, pdMS_TO_TICKS(timeout_ms));

    this->connection_succeeded = !timed_out;

    return !timed_out;
}

// -----------------------------------------------------------------------------
// dtethernet check if is connected
static bool
dtethernet_is_connected(dtethernet_espidf_t* this)
{
    return dtethernet_connected_waitfunc(this, NULL, 0);
}

// -----------------------------------------------------------------------------
// dtethernet dispose function
static void
dtethernet_dispose(dtethernet_espidf_t* this)
{
    vEventGroupDelete(this->event_group_handle);
}

// -----------------------------------------------------------------------------
// dtethernet init function
dterr_t*
dtethernet_init(dtethernet_espidf_t* this)
{
    this->connect = dtethernet_connect;
    this->configure = dtethernet_configure;
    this->is_connected = dtethernet_is_connected;
    this->dispose = dtethernet_dispose;

    s_semph_get_ip_addrs = xSemaphoreCreateBinary();
    if (s_semph_get_ip_addrs == NULL)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to create semaphore");
    }

    // init data members
    this->event_group_handle = xEventGroupCreate();
    if (this->event_group_handle == NULL)
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "failed to create event group");
    }

    this->connection_succeeded = false;

    return NULL;
}