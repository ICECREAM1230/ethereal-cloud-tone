#pragma once
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "status_led_pattern.h"

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3)
void status_led_init(void);
void status_led_boot_complete(bool spi_ok);
void status_led_engine_heartbeat(void);
void status_led_voice_heartbeat(void);
void status_led_set_flag(status_led_flag_t flag, bool enabled);
#else
// Host regression and legacy shared targets do not operate status GPIOs.
static inline void status_led_init(void) {}
static inline void status_led_boot_complete(bool spi_ok) { (void)spi_ok; }
static inline void status_led_engine_heartbeat(void) {}
static inline void status_led_voice_heartbeat(void) {}
static inline void status_led_set_flag(status_led_flag_t flag, bool enabled)
{ (void)flag; (void)enabled; }
#endif
