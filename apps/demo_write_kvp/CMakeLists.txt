cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig.freertos_stats"
)

set(DTCORE_LIBRARY         "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtcore/dtcore_library")
set(DTMC_BASE_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtmc_base/dtmc_base_library")
set(DTMC_BASE_DEMOS        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtmc_base/dtmc_base_demos")
set(DTMC_ESPIDF_LIBRARY    "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_espidf_library")

set(EXTRA_COMPONENT_DIRS 
    ${DTCORE_LIBRARY}
    ${DTMC_BASE_LIBRARY} 
    ${DTMC_BASE_DEMOS}
    ${DTMC_ESPIDF_LIBRARY} 
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)


