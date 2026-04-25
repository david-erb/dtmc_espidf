#pragma once

#include <stdint.h>

#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttimeseries.h>

#define MAIN_MCP4728_CHANNEL_COUNT 4
#define MAIN_NETPORTAL_TOPIC "belleville/demo/netportal/topic"

typedef struct main_config_t
{
    dttimeseries_handle timeseries_handles[MAIN_MCP4728_CHANNEL_COUNT];
} main_config_t;

typedef struct main_t
{
    main_config_t config;
} main_t;

extern dterr_t*
main_mcp4728_entrypoint(void* context, dttasker_handle tasker_handle);