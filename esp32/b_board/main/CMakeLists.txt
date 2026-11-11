idf_component_register(SRCS "b_entry.c" "b_runtime.c" "board_io.c" "usb_midi.c" "ble_midi_tx.c" "b_router.c" "b_storage.c" "b_console.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES harp_core b_control esp_driver_gpio esp_driver_uart esp_timer bt nvs_flash esp_hw_support esp_tinyusb)
