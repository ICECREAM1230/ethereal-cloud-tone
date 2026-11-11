# The maintained ESP32-S3 controller component lives in main/.
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.esp32s3")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
set(PROJECT_VER "ae350-dds-depth-20261104")
project(tang_mega_138k_synth_ctrl)
idf_build_get_property(fm_check_python PYTHON)
add_custom_target(fm_contract_check ALL
    COMMAND "${fm_check_python}" -B "${CMAKE_CURRENT_LIST_DIR}/tools/check_fm_contract.py"
    VERBATIM)
idf_component_get_property(fm_main_library main COMPONENT_LIB)
add_dependencies(${fm_main_library} fm_contract_check)
add_custom_target(dds_factory_check ALL
    COMMAND "${fm_check_python}" -B "${CMAKE_CURRENT_LIST_DIR}/tools/generate_dds_factory.py" --check
    VERBATIM)
add_dependencies(${fm_main_library} dds_factory_check)

# Generate a Safety-WL filesystem artifact without including it in flash_args.
# IDF 6.1's helper does not forward --wl_mode, so call its official generator
# explicitly. Ordinary firmware updates must not overwrite User documents.
file(GLOB_RECURSE dds_seed_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/storage/*")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/dds_storage.bin"
    COMMAND "${fm_check_python}" "$ENV{IDF_PATH}/components/fatfs/wl_fatfsgen.py"
            "${CMAKE_CURRENT_LIST_DIR}/storage" --partition_size 0x200000
            --sector_size 512 --sectors_per_cluster 1 --wl_mode safe
            --long_name_support --fat_count 2 --use_default_datetime
            --output_file "${CMAKE_BINARY_DIR}/dds_storage.bin"
    DEPENDS ${dds_seed_files} VERBATIM)
add_custom_target(dds_storage_image ALL DEPENDS "${CMAKE_BINARY_DIR}/dds_storage.bin")
