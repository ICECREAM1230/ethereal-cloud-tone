/* Descriptor structure follows the MIT-licensed Espressif ESP-IDF tusb_midi
 * example (2019 Ha Thach; 2022-2025 Espressif Systems). No A-board code is used. */
#include "platform.h"
#include <stdio.h>
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t generation=1;
static bool attached;
static char serial[13];
static const char *strings[]={ (const char[]){0x09,0x04}, "Laser Harp", "Laser Harp B", serial, "B MIDI" };
static const uint8_t config[]={
    TUD_CONFIG_DESCRIPTOR(1,2,0,TUD_CONFIG_DESC_LEN+TUD_MIDI_DESC_LEN,0,100),
    TUD_MIDI_DESCRIPTOR(0,4,1,0x81,64),
};

static void event(tinyusb_event_t *e,void *arg)
{
    (void)arg;
    if(e->id!=TINYUSB_EVENT_ATTACHED && e->id!=TINYUSB_EVENT_DETACHED) return;
    portENTER_CRITICAL(&lock);
    attached=e->id==TINYUSB_EVENT_ATTACHED;
    if(++generation==0) ++generation;
    portEXIT_CRITICAL(&lock);
}

esp_err_t b_usb_init(void)
{
    uint8_t mac[6];
    esp_err_t rc=esp_read_mac(mac,ESP_MAC_WIFI_STA);
    if(rc!=ESP_OK) return rc;
    snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    tinyusb_config_t cfg=TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.string=strings;
    cfg.descriptor.string_count=sizeof(strings)/sizeof(strings[0]);
    cfg.descriptor.full_speed_config=config;
    cfg.event_cb=event;
    return tinyusb_driver_install(&cfg);
}

bool b_usb_ready(uint32_t *epoch)
{
    portENTER_CRITICAL(&lock);
    bool ready=attached;*epoch=generation;
    portEXIT_CRITICAL(&lock);
    return ready && tud_midi_mounted() && !tud_suspended();
}

bool b_usb_send(void *arg,const harp_midi_t *m,uint32_t epoch)
{
    (void)arg;uint32_t current;uint8_t packet[4];
    return b_usb_ready(&current) && current==epoch && harp_usb_encode(m,packet) && tud_midi_packet_write(packet);
}

void b_usb_drain(void)
{
    uint8_t packet[4];
    for(unsigned i=0;i<8 && tud_midi_available();i++)
        if(!tud_midi_packet_read(packet)) break;
}
