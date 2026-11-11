// Board assignments; ESP32-S3 PMOD adapter and active-low status LEDs.
#pragma once

#define FPGA_SPI_FREQ_HZ 10000000u

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define DIN_MIDI_RX_PIN 16
// User correction 2026-10-11: J8 pins 6/5 are GPIO21/47;
// pins 8/7 are GPIO13/14. SPI GPIO numbers are unchanged.
#define FPGA_SPI_CS_PIN   21
#define FPGA_SPI_SCLK_PIN 47
#define FPGA_SPI_MOSI_PIN 13
#define FPGA_SPI_MISO_PIN 14
#define BOARD_STATUS_LEDS 1
#define STATUS_LED_RED_PIN   38
#define STATUS_LED_GREEN_PIN 2
#define STATUS_LED_BLUE_PIN  1
// Keep panel inputs outside the PMOD routes and the GPIO1/2 LEDs.
#define PANEL_MIDI_RX_PIN 42
#define SLIDER_ADC_GPIO_PIN 8
#define BOARD_SLIDER_ENABLED 1
#define BOARD_SLIDER_ACTIVE 1
#else
// Shared source retains the original C3 assignments; S3 is the product target.
#define DIN_MIDI_RX_PIN 0
#define FPGA_SPI_CS_PIN   7
#define FPGA_SPI_SCLK_PIN 4
#define FPGA_SPI_MOSI_PIN 6
#define FPGA_SPI_MISO_PIN 5
#define BOARD_STATUS_LEDS 0
#define PANEL_MIDI_RX_PIN 2
#define SLIDER_ADC_GPIO_PIN 1
#define BOARD_SLIDER_ENABLED 1
#define BOARD_SLIDER_ACTIVE 0
#endif
