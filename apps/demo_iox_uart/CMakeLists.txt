cmake_minimum_required(VERSION 3.16)

# When any of these sdkconfig files changes you must:
# rm -rf sdkconfig sdkconfig.old build && idf.py set-target esp32 && idf.py reconfigure && idf.py build

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig.freertos_stats"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig.system"
)

include(${CMAKE_CURRENT_LIST_DIR}/../../dthelpers.cmake)
dthelpers_resolve(DTMC_ESPIDF_DIR dtmc_espidf "dtmc_espidf repo directory")
dthelpers_resolve(DTMC_BASE_DIR dtmc_base "dtmc_base repo directory")
dthelpers_resolve(DTCORE_DIR dtcore "dtcore repo directory")

set(EXTRA_COMPONENT_DIRS 
    ${DTMC_ESPIDF_DIR}/dtmc_espidf_library
    ${DTMC_BASE_DIR}/dtmc_base_demos
    ${DTMC_BASE_DIR}/dtmc_base_library
    ${DTCORE_DIR}/dtcore_library
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)


