idf_component_register(
    SRCS "app_main.cpp" "app_driver.cpp" "breathing.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash app_update
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
