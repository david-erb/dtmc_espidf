include(${CMAKE_CURRENT_LIST_DIR}/../dthelpers.cmake)
dthelpers_glob(MY_SOURCES dt*.c)

# -----------------------------------------------------------------------------
# source groups

set(ETH_OPENETH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dtethernet_espidf.c
)

# pre-remove conditional source groups
list(REMOVE_ITEM MY_SOURCES ${ETH_OPENETH_SOURCES})

# -----------------------------------------------------------------------------
# Base requires (always)
set(MY_REQUIRES
    dtcore_library
    dtmc_base_library
    esp_timer
    nvs_flash esp_event esp_wifi
    esp_netif esp_eth lwip
    mqtt
    driver
    esp_driver_twai
    esp_adc
)

# -----------------------------------------------------------------------------
# Ethernet gating: require both "ethernet enabled" AND "subtype openeth"
if(CONFIG_ETH_ENABLED AND CONFIG_ETH_USE_OPENETH)
    message(STATUS "dtmc_espidf_library: Ethernet enabled + OpenETH selected -> adding dtethernet_espidf.c")
    list(APPEND MY_SOURCES ${ETH_OPENETH_SOURCES})
else()
    if(NOT CONFIG_ETH_ENABLED)
        message(STATUS "dtmc_espidf_library: Ethernet disabled (CONFIG_ETH_ENABLED=0) -> not adding OpenETH sources")
    elseif(NOT CONFIG_ETH_USE_OPENETH)
        message(STATUS "dtmc_espidf_library: Ethernet enabled but OpenETH not selected (CONFIG_ETH_USE_OPENETH=0) -> not adding OpenETH sources")
    endif()
endif()

# -----------------------------------------------------------------------------
idf_component_register(
    SRCS ${MY_SOURCES}
    INCLUDE_DIRS include
    REQUIRES ${MY_REQUIRES}
)

set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LINK_INTERFACE_MULTIPLICITY 3)