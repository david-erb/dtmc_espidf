if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE AND NOT DEFINED CMAKE_FIND_PACKAGE_NAME)
    file(GLOB MY_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dt*.c)
else()
    file(GLOB MY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/dt*.c)
endif()

idf_component_register(
    SRCS ${MY_SOURCES}
    INCLUDE_DIRS include
    REQUIRES 
        dtcore_library
        dtmc_base_library
        dtmc_espidf_library
        esp-modbus
)
