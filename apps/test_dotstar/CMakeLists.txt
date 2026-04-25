cmake_minimum_required(VERSION 3.16)

# When either of these changes you must:
# rm -f sdkconfig sdkconfig.old build
# idf.py set-target esp32
# idf.py reconfigure

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig.freertos_stats"
)

set(DTCORE_LIBRARY         "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtcore/dtcore_library")
set(DTCORE_TESTS           "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtcore/dtcore_tests")
set(DTMC_BASE_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtmc_base/dtmc_base_library")
set(DTMC_BASE_TESTS        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtmc_base/dtmc_base_tests")
set(DTMC_ESPIDF_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_espidf_library")
set(DTMC_ESPIDF_TESTS        "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_espidf_tests")

set(EXTRA_COMPONENT_DIRS 
    ${DTCORE_LIBRARY}
    ${DTCORE_TESTS}
    ${DTMC_BASE_LIBRARY} 
    ${DTMC_BASE_TESTS}
    ${DTMC_ESPIDF_LIBRARY} 
    ${DTMC_ESPIDF_TESTS}
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)


