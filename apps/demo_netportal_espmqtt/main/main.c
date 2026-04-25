#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker_registry.h>

// platform specific netportal
#include <dtmc/dtnetportal_espmqtt.h>

// prepper for network setup
#include <dtmc/dtprepper_espidf.h>

#include <dtmc_base_demos/demo_netportal.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
void
app_main(void)
{
    dterr_t* dterr = NULL;
    dtprepper_espidf_t _prepper = { 0 }, *prepper = &_prepper;
    dtnetportal_handle netportal_handle = NULL;

    demo_t* demo = NULL;

    DTERR_C(dtprepper_espidf_init(prepper));

    // normally we get radioconfig from NVS, but for testing we can set it here
    prepper->radioconfig.mqtt_host = dtstr_dup("mqtt://192.168.0.9");
    prepper->radioconfig.mqtt_port = 1883;
    prepper->radioconfig.self_node_name = dtstr_dup("Node1");
    prepper->radioconfig.wifi_ssid = dtstr_dup("Tele2_3cf151");
    prepper->radioconfig.wifi_password = dtstr_dup("j6uyyfep");

    {
        char s[512];
        dtradioconfig_to_string(&prepper->radioconfig, s, sizeof(s));
        dtlog_info(TAG, "radioconfig:\n%s", s);
    }

    DTERR_C(prepper->configure(prepper));

    DTERR_C(prepper->start_system(prepper));

    DTERR_C(prepper->start_ethernet(prepper));
    dtlog_info(TAG, "ethernet started");

    dtlog_info(TAG, "-----------------------------------------------------------------");
    dtlog_info(TAG, "free heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    dtlog_info(TAG, "largest free block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    //  ==== the currently registered tasks ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_register_tasks(&dttasker_registry_global_instance));
        DTERR_C(dttasker_registry_format_as_table(&dttasker_registry_global_instance, &s));
        dtlog_info(TAG, "Tasks:\n%s", s);
        dtstr_dispose(s);
    }

    // === the netportal ===
    {
        dtnetportal_espmqtt_t* o = NULL;
        DTERR_C(dtnetportal_espmqtt_create(&o));
        netportal_handle = (dtnetportal_handle)o;
        dtnetportal_espmqtt_config_t c = { 0 };
        DTERR_C(dtnetportal_espmqtt_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.netportal_handle = netportal_handle;
        c.is_server = false;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the objects
    dtnetportal_dispose(netportal_handle);
}
