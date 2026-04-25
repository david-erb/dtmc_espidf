cmake_minimum_required(VERSION 3.16)

set(DTCORE_LIBRARY         "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtcore/dtcore_library")
set(DTMC_BASE_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../../../dtack-c/dtmc_base/dtmc_base_library")
set(DTMC_ESPIDF_LIBRARY      "${CMAKE_CURRENT_SOURCE_DIR}/../../dtmc_espidf_library")

set(EXTRA_COMPONENT_DIRS 
    ${DTCORE_LIBRARY}
    ${DTMC_BASE_LIBRARY} 
    ${DTMC_ESPIDF_LIBRARY} 
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)


