cmake_minimum_required(VERSION 3.16)

# When any of these sdkconfig files changes you must:
# rm -rf sdkconfig sdkconfig.old build
# idf.py set-target esp32
# idf.py reconfigure
# all at once: rm -rf sdkconfig sdkconfig.old build && idf.py set-target esp32 && idf.py reconfigure && idf.py build


set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sdkconfig.freertos_stats"
)

set(DTMC_ESPIDF_LIBRARY    "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_espidf_library")
set(DTMC_BASE_DEMOS        "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_demos")
set(DTMC_BASE_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_base_library")
set(DTCORE_LIBRARY         "${CMAKE_CURRENT_SOURCE_DIR}/../../dtcore_library")

set(EXTRA_COMPONENT_DIRS 
    ${DTMC_ESPIDF_LIBRARY} 
    ${DTMC_BASE_DEMOS}
    ${DTMC_BASE_LIBRARY} 
    ${DTCORE_LIBRARY}
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)


