// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_app_desc.h"

#include "spi_regs.h"
#include "midi_log.h"
#include "event_bus.h"
#include "engine_link.h"
#include "voice_alloc.h"
#include "slider.h"
#include "ble_midi.h"
#include "stress_test.h"
#include "wifi_web.h"
#include "board_pins.h"
#include "status_led.h"
#include "dds_presets.h"
#include "ks_timbre_control.h"
#include "vendor/tomlc17/tomlc17.h"
#include "raw_midi.h"
#include "esp_timer.h"
#include "fm_only_contract.h"
#include "fm_contract.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "driver/gpio.h"
#endif

void app_main(void)
{
    // Boot banner: WHICH build is running (esp_app_desc is regenerated
    // every build — version is git-describe, date/time is the compile).
    // Exists because a hash-verified flash once left the old app running
    // (USB-JTAG hard-reset quirk): trust this line, not esptool's exit.
    const esp_app_desc_t *ad = esp_app_get_description();
    printf("=== ect fw %s (built %s %s) ===\n",
           ad->version, ad->date, ad->time);
    status_led_init();

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    // Event bus first, then the consumers that subscribe to it.
    event_bus_init();
    midi_log_init();

    // The FPGA-owned performance build accepts native USB, Web and BLE.
    // Production source3 is native FPGA sensors (FDC/dual-ToF selection).
    // The old DIN/panel UART source is excluded from controller_sources.

    // Keep 10 MHz for the engine's 1 ms tick; validate the new wiring
    // with the word-protocol self-test below before testing MIDI.
    fpga_spi_init(FPGA_SPI_MOSI_PIN, FPGA_SPI_MISO_PIN,
                  FPGA_SPI_SCLK_PIN, FPGA_SPI_CS_PIN, FPGA_SPI_FREQ_HZ);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // IDF may route SPIQ output as well as input to MISO for dual-I/O support.
    // This board uses single-line full-duplex SPI with FPGA-driven MISO only.
    // Force pad OE off while retaining the SPIQ input matrix connection.
    ESP_ERROR_CHECK(gpio_output_disable(FPGA_SPI_MISO_PIN));
    ESP_ERROR_CHECK(gpio_input_enable(FPGA_SPI_MISO_PIN));
    ESP_ERROR_CHECK(gpio_set_pull_mode(FPGA_SPI_MISO_PIN, GPIO_FLOATING));
    printf("=== PMOD1 SPI: MOSI=%d MISO=%d SCLK=%d CS=%d; MISO input only ===\n",
           FPGA_SPI_MOSI_PIN, FPGA_SPI_MISO_PIN, FPGA_SPI_SCLK_PIN, FPGA_SPI_CS_PIN);
    ESP_ERROR_CHECK(gpio_dump_io_configuration(stdout,
        (1ULL << 0) | (1ULL << 19) | (1ULL << 20) |
        (1ULL << FPGA_SPI_MOSI_PIN) | (1ULL << FPGA_SPI_MISO_PIN) |
        (1ULL << FPGA_SPI_SCLK_PIN) | (1ULL << FPGA_SPI_CS_PIN) |
        (1ULL << STATUS_LED_RED_PIN) | (1ULL << STATUS_LED_GREEN_PIN) |
        (1ULL << STATUS_LED_BLUE_PIN)));
#endif

    uint32_t validation_identity[2]={0};
    bool validation_identity_ok=fpga_word_read_burst(FM_ONLY_REG_IDENTITY,validation_identity,2);
    if(CONFIG_FPGA_FM_ONLY) {
        if(!validation_identity_ok || validation_identity[0]!=FM_ONLY_IDENTITY ||
           validation_identity[1]!=FM_ONLY_VERSION) {
            printf("FM_ONLY firmware requires FMV1 validation image ABI 1.0; initialization stopped\n");
            status_led_boot_complete(false);return;
        }
        printf("=== FM_ONLY validation firmware / FMV1 ABI 1.0 ===\n");
    } else if(validation_identity_ok && validation_identity[0]==FM_ONLY_IDENTITY) {
        printf("FMV1 validation image requires explicit FM_ONLY firmware; initialization stopped\n");
        status_led_boot_complete(false);return;
    }

    // Before touching old live registers, cancel any surviving FPGA-side
    // performance session from a previous ESP reset. Poll in separate slices;
    // USB-wide ENABLE acknowledgement may take up to the host barrier bound.
    raw_midi_init();
    if(!CONFIG_FPGA_AE350_CONTROL) {
    int64_t raw_boot_deadline=esp_timer_get_time()+150000;
    raw_midi_state_t raw_boot;
    do {
        raw_midi_service(esp_timer_get_time()+250);
        raw_midi_state(&raw_boot);
        if(!raw_boot.available || raw_boot.initialized)break;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while(esp_timer_get_time()<raw_boot_deadline);
    if(raw_boot.available && !raw_boot.initialized) {
        printf("raw MIDI disable barrier not confirmed; synth initialization stopped\n");
        status_led_boot_complete(false);
        return;
    }

    }
    // CPU1 performs its first capability/ABI probe inside the sole engine
    // SPI owner. No boot writes or legacy fallback precede that probe.
    // ENG1 hardware can already be performing under physical ownership.
    // Its boot diagnostic must not overwrite the shared producer shadow.
    uint32_t engine_magic=0;
    bool engine_probe_ok=fpga_word_read_burst(0x070b,&engine_magic,1);
    bool writable_selftest=!CONFIG_FPGA_AE350_CONTROL && engine_probe_ok && engine_magic!=0x454e4731u;
    // Word-protocol self-test (spi_bus.sv / docs/memory_map.md format).
    // Mirrors tb_spi_bus.sv's cases so firmware and RTL agree on the
    // same evidence.
    int fails = 0;
    if(writable_selftest) {

    printf("=== 1. single word write/read ===\n");
    fpga_word_write(0x0005, 0xDEADBEEF);
    uint32_t v = fpga_word_read(0x0005);
    printf("word[0x0005] = %08lX %s\n", (unsigned long)v,
           v == 0xDEADBEEF ? "OK" : "FAIL");
    fails += (v != 0xDEADBEEF);

#if CONFIG_FPGA_FM_ONLY
    printf("=== 2. burst auto-increment (FM capability, read-only) ===\n");
    uint32_t capability[6]={0};
    bool id_ok=fpga_word_read_burst(FM_REG_CAP_MAGIC,capability,6);
    bool cap_ok=id_ok && capability[0]==FM_CAP_MAGIC && fm_abi_compatible(capability[1]) &&
        capability[2]==FM_BACKEND_JT51_OPM && capability[3]==8 && capability[4]==4;
    printf("FM capability burst and frame ID: %s\n",cap_ok?"OK":"FAIL");
    fails+=!cap_ok;
#else
    printf("=== 2. burst auto-increment (LFO region) ===\n");
    uint32_t wq[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    uint32_t rq[4] = {0};
    fpga_word_write_burst(0x0100, wq, 4);
    bool id_ok = fpga_word_read_burst(0x0100, rq, 4);
    for (int i = 0; i < 4; i++) {
        printf("word[0x%04X] = %08lX %s\n", 0x0100 + i, (unsigned long)rq[i],
               rq[i] == wq[i] ? "OK" : "FAIL");
        fails += (rq[i] != wq[i]);
    }
    printf("ID byte on read frame: %s\n", id_ok ? "OK (A5)" : "FAIL");
    fails += !id_ok;
#endif

    printf("=== 3. out-of-window: dropped and zero, no alias ===\n");
    // 0x1005 aliases 0x0005 under the 11-bit backed mask AND decodes
    // to nothing else. The previous probe address, 0x0805, was inside
    // the BUS POOL (0x0800-0x0BFF): the store dropped it as intended,
    // but spi_bus fired the bus mailbox and parked bus 5 at a large
    // negative base every boot - a self-test moving a live fader
    // (issue #103). Probe addresses must miss EVERY decode window:
    // backed 0x0000-0x07FF, bus 0x0800-0x0BFF, elements 0x2000-0x5FFF.
    fpga_word_write(0x1005, 0xBAD0BAD0);      // would alias 0x0005 if broken
    uint32_t z = fpga_word_read(0x1005);
    uint32_t keep = fpga_word_read(0x0005);
    printf("word[0x1005] = %08lX %s, word[0x0005] = %08lX %s\n",
           (unsigned long)z, z == 0 ? "OK" : "FAIL",
           (unsigned long)keep, keep == 0xDEADBEEF ? "OK" : "FAIL");
    fails += (z != 0) + (keep != 0xDEADBEEF);

    printf("=== RESULT: %s (%d fails) ===\n", fails ? "FAIL" : "ALL OK", fails);

    } else {
        printf("ENG1/unknown control ownership: destructive boot SPI self-test skipped\n");
    }

    // ── The synth proper ────────────────────────────────────────────
    // engine_link takes sole ownership of the SPI link from here on
    // (mutes both banks at init), then the control owner initializes static
    // routing/templates. FPGA alone allocates USB/Web/BLE performance voices.
    if(CONFIG_FPGA_AE350_CONTROL)
        printf("=== AE350 CPU1 ABI2 / esp-compat-20261030: Web/BLE RAW + templates; CPU voice owner ===\n");
    else printf("=== FPGA-owned MIDI: native USB + Web/BLE raw; S3 parameter control ===\n");
    engine_link_init();
    toml_option_t document_option=toml_default_option();
    document_option.check_utf8=true;toml_set_option(document_option);
    dds_presets_init(); // before owner starts: mount/recover/startup parsing
    ks_timbre_store_init();
    voice_alloc_init();
    slider_init();   // S3: GPIO8 ADC/CC71 + NVS/USB console; GPIO1 remains LED.
    ble_midi_init(); // MIDI over BLE: advertise "FPGA_Bluetooth" (standard MIDI service)
    wifi_web_start(); // optional local SoftAP, below musical task priorities
    status_led_boot_complete(fails == 0);
#if CONFIG_ECT_STRESS_TEST
    stress_test_start();  // synthetic MIDI flood (#70 repro); off by default
#endif
    // NOTE: boot-capture tooling waits for the "play the keyboard"
    // marker - keep the prefix if this line ever changes.
    printf("play the keyboard\n");   // (the gate-by-gain/clicks caveat
                                     // predates the ADSR era - #110)
}
