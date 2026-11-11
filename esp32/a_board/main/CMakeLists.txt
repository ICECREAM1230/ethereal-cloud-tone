# Additive synthesis is unfinished and not integrated (2026-10-24).
# Retain additive_link.c, additive_patch.c and their contracts for future work;
# they are deliberately excluded from the production controller_sources.
set(controller_sources "main.c" "event_bus.c" "midi_log.c"
                       "control_source.c" "control_api.c" "ble_control.c" "ble_control_codec.c"
                       "midi_parser.c" "spi_regs.c" "engine_link.c"
                       "voice_alloc.c" "slider.c" "ble_midi.c" "patch.c"
                       "stress_test.c" "web_control.c" "wifi_web.c" "ks_params.c" "ks_voice.c" "ks_timbre_params.c" "ks_timbre_document.c" "ks_timbre_control.c"
                       "status_led.c" "status_led_pattern.c" "fm_voice.c"
                       "dds_patch_units.c" "dds_document.c" "dds_files.c" "dds_presets.c"
                       "raw_midi.c" "ble_midi_unframer.c" "web_raw_midi.c" "web_raw_queue.c"
                       "midi_templates.c" "midi_template_link.c" "ae350_control.c" "fdc_control.c"
                       "parameter_cc.c" "midi_feedback_packet.c" "midi_feedback_guard.c"
                       "midi_feedback_transport.c" "midi_feedback.c"
                       "vendor/tomlc17/tomlc17.c")
set(controller_requires spi_flash esp_app_format esp_driver_spi esp_driver_gpio esp_driver_uart esp_timer
                        esp_adc nvs_flash esp_driver_usb_serial_jtag bt esp_ringbuf
                        esp_wifi esp_event esp_netif esp_http_server fatfs wear_levelling)
# ESP-IDF's early dependency expansion runs before sdkconfig is available.
# Disabled builds compile wifi_web.c as a stub; unused Wi-Fi objects are not linked.
idf_component_register(SRCS ${controller_sources} PRIV_REQUIRES ${controller_requires} INCLUDE_DIRS "" "${CMAKE_CURRENT_LIST_DIR}/../contract/ks_timbre/generated/c")

# Use actual target/compiler frames to guard the owner snapshot call chain.
set_source_files_properties("voice_alloc.c" "engine_link.c" "ks_timbre_params.c" PROPERTIES COMPILE_OPTIONS "-fstack-usage;-fcallgraph-info=su")
set_source_files_properties("control_source.c" "control_api.c" "ble_control.c" "ble_control_codec.c"
    PROPERTIES COMPILE_OPTIONS "-fstack-usage;-fcallgraph-info=su")
set_source_files_properties("wifi_web.c" "ble_midi.c" "ble_midi_unframer.c" "midi_feedback_guard.c"
    "raw_midi.c" "ks_timbre_document.c" "ks_timbre_control.c" "fdc_control.c"
    "web_control.c" "web_raw_midi.c" "web_raw_queue.c" "event_bus.c" "parameter_cc.c"
    "vendor/tomlc17/tomlc17.c"
    PROPERTIES COMPILE_OPTIONS "-fstack-usage;-fcallgraph-info=su")
add_custom_target(check_voice_owner_stack ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_owner_stack.py"
            --callgraph "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${COMPONENT_LIB}.dir/voice_alloc.c.ci"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/voice_alloc.c"
            --report "${CMAKE_CURRENT_BINARY_DIR}/voice-owner-stack.json"
    DEPENDS ${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_owner_stack.py"
    VERBATIM)
add_custom_target(check_ble_control_stack ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tests/host/test_ble_control_stack.py"
            --build "${CMAKE_BINARY_DIR}"
            --report "${CMAKE_CURRENT_BINARY_DIR}/ble-control-stack.json"
    DEPENDS ${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../tests/host/test_ble_control_stack.py"
    VERBATIM)
if(CONFIG_ECT_WIFI_WEB)
    set(compressed_page "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
    add_custom_command(OUTPUT "${compressed_page}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/compress_web.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" "${compressed_page}"
        DEPENDS "web/index.html" "../tools/compress_web.py"
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${compressed_page}" BINARY RENAME_TO index_html_gz)
endif()

# Pinned upstream parser has an ESP32 uint32_t printf warning; keep it unmodified.
set_property(SOURCE "vendor/tomlc17/tomlc17.c" APPEND PROPERTY COMPILE_OPTIONS "-Wno-format")

# The KS model compiler has a bounded temporary image on the owner stack.
add_custom_target(check_ks_timbre_owner_stack ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_ks_timbre_stack.py"
            --owner "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${COMPONENT_LIB}.dir/voice_alloc.c.ci"
            --compiler "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${COMPONENT_LIB}.dir/ks_timbre_params.c.ci"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/voice_alloc.c"
            --report "${CMAKE_CURRENT_BINARY_DIR}/ks-timbre-owner-stack.json"
    DEPENDS ${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_ks_timbre_stack.py"
    VERBATIM)
