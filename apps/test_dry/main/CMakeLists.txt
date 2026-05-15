idf_component_register(
    SRCS main.c
    REQUIRES
      dtcore_library
      dtcore_tests
      dtmc_base_library
      dtmc_base_tests
      dtmc_espidf_library
      dtmc_espidf_tests
      dtmc_services_library
      dtmc_services_tests
)
