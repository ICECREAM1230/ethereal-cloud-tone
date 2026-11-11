#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "board_pins.h"
#include "status_led_pattern.h"

static status_led_state_t running(uint32_t now)
{
    return (status_led_state_t){.boot_complete=true, .boot_spi_ok=true,
        .engine_seen=true, .voice_seen=true, .engine_ms=now, .voice_ms=now};
}
static void expect(status_led_state_t s, uint32_t time, int red, int green, int blue)
{
    status_led_levels_t levels=status_led_pattern(&s,time);
    if(levels.red!=red || levels.green!=green || levels.blue!=blue)
        fprintf(stderr,"time=%u levels=%d/%d/%d expected=%d/%d/%d\n",
                time,levels.red,levels.green,levels.blue,red,green,blue);
    assert(levels.red==red && levels.green==green && levels.blue==blue);
}
int main(void)
{
    // Independent wiring expectation from the user's PMOD adapter table.
    _Static_assert(BOARD_STATUS_LEDS && BOARD_SLIDER_ENABLED && BOARD_SLIDER_ACTIVE &&
                   PANEL_MIDI_RX_PIN==42 && SLIDER_ADC_GPIO_PIN==8,
                   "panel inputs must be restored on GPIO42/8");
    _Static_assert(STATUS_LED_RED_PIN==38 && STATUS_LED_GREEN_PIN==2 && STATUS_LED_BLUE_PIN==1,
                   "LED schematic GPIO assignments changed");
    _Static_assert(FPGA_SPI_CS_PIN==21 && FPGA_SPI_SCLK_PIN==47 &&
                   FPGA_SPI_MOSI_PIN==13 && FPGA_SPI_MISO_PIN==14 && DIN_MIDI_RX_PIN==16,
                   "PMOD SPI/DIN mapping changed");
    const int outputs[]={STATUS_LED_RED_PIN,STATUS_LED_GREEN_PIN,STATUS_LED_BLUE_PIN,
                         FPGA_SPI_MOSI_PIN,FPGA_SPI_SCLK_PIN,FPGA_SPI_CS_PIN};
    for(unsigned i=0;i<sizeof(outputs)/sizeof(outputs[0]);i++) {
        assert(outputs[i]!=FPGA_SPI_MISO_PIN && outputs[i]!=DIN_MIDI_RX_PIN &&
               outputs[i]!=PANEL_MIDI_RX_PIN && outputs[i]!=SLIDER_ADC_GPIO_PIN);
        assert(outputs[i]!=0 && outputs[i]!=19 && outputs[i]!=20 && outputs[i]!=45 && outputs[i]!=48);
        for(unsigned j=0;j<i;j++) assert(outputs[i]!=outputs[j]);
    }
    // Low means ON. Boot is a double blue flash, two short pulses per 2 s.
    status_led_state_t s={0};
    expect(s,0,1,1,0); expect(s,99,1,1,0); expect(s,100,1,1,1);
    expect(s,200,1,1,0); expect(s,300,1,1,1); expect(s,2000,1,1,0);
    expect(s,4999,1,1,1); expect(s,5000,0,1,1); expect(s,5150,1,1,1);
    // Only completed work in BOTH owners keeps the running heartbeat alive.
    s=running(1000); expect(s,1000,1,1,0); expect(s,1100,1,1,1);
    s.engine_ms=s.voice_ms=1999; expect(s,2000,1,1,0);
    s.engine_ms=1000; expect(s,2000,0,1,1); expect(s,2125,1,1,1);
    s.engine_ms=2200; s.voice_ms=1000; expect(s,2250,0,1,1);
    s.engine_seen=false; s.running_ms=3000;
    expect(s,3500,1,1,1); expect(s,4000,0,1,1); // startup grace expires
    s=running(4000); expect(s,4000,1,1,0); // a recovered owner clears transient stall
    // Readback recovery blinks at 2 Hz and stops after current recovery clears.
    s=running(2250); s.engine_recovering=true;
    expect(s,2250,1,1,1); expect(s,2500,0,1,1);
    s.engine_recovering=false; expect(s,2500,1,1,1);
    // Boot SPI failure is latched and dominates all transient red patterns.
    s.boot_spi_ok=false; s.flags=STATUS_BLE_FAULT;
    expect(s,2300,0,1,1); expect(s,2400,0,1,1);
    // Green distinguishes radio availability from an actual peer connection.
    s=running(0); s.flags=STATUS_BLE_CONNECTED; expect(s,150,1,1,1);
    s.flags=STATUS_BLE_READY; expect(s,0,1,0,0); expect(s,150,1,1,1);
    s.flags|=STATUS_BLE_CONNECTED; expect(s,150,1,0,1);
    s.flags|=STATUS_WIFI_READY|STATUS_WIFI_CONNECTED;
    s.flags&=~STATUS_BLE_CONNECTED; expect(s,150,1,0,1); // other transport stays connected
    s.flags&=~STATUS_WIFI_CONNECTED; expect(s,150,1,1,1);
    s.flags=STATUS_WIFI_READY; expect(s,0,1,0,0); expect(s,150,1,1,1);
    // Disabled Wi-Fi has no fault flag; actual service failure double-flashes red.
    s=running(200); s.flags=STATUS_BLE_FAULT;
    expect(s,200,0,1,1); expect(s,300,1,1,1);
    s.flags=STATUS_WIFI_FAULT; expect(s,200,0,1,1);
    s.flags=0; expect(s,200,1,1,1);
    // uint32 millisecond wrap (49.7 days) must preserve freshness and phase.
    s=running(499); s.started_ms=UINT32_MAX-500u;
    s.running_ms=s.started_ms;
    expect(s,499,1,1,0);
    s.engine_ms=UINT32_MAX-500u; expect(s,499,0,1,1);
    puts("S3 PMOD GPIO ownership and active-low LED boot/heartbeat/peer/fault/wrap patterns PASS");
    return 0;
}
