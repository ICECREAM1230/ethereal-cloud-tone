idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES fdc2214 esp_driver_i2c esp_driver_gpio esp_driver_usb_serial_jtag esp_timer esp_system console
    PRIV_REQUIRES volume_audio esp_driver_i2s)
# ESP-IDF expands requirements before loading CONFIG values. Only sources
# are conditional; unreferenced audio code is omitted from the raw-only image.
if(CONFIG_FDC_AUDIO_ENABLE)
    target_sources(${COMPONENT_LIB} PRIVATE "audio_output.c")
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
