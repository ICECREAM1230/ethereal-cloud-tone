#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board_pins.h"
#include "midi_in.c"

static int s_rx[2]={-1,-1}, s_idle=1, s_inverse, s_tasks, s_events;
static uint64_t s_input_mask;
static evt_t s_captured[8];
esp_err_t gpio_config(const gpio_config_t *cfg)
{ assert(cfg->mode==GPIO_MODE_INPUT);s_input_mask=cfg->pin_bit_mask;return ESP_OK; }
int gpio_get_level(int gpio) { assert(gpio==42);return s_idle; }
void esp_rom_delay_us(unsigned delay) { assert(delay==300); }
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue_size, void *queue, int flags)
{ assert((port==0||port==1) && rx==256 && tx==0 && queue_size==0 && !queue && !flags);return ESP_OK; }
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *cfg)
{ assert(port==0||port==1);assert(cfg->baud_rate==31250 && cfg->data_bits==UART_DATA_8_BITS && cfg->parity==UART_PARITY_DISABLE && cfg->stop_bits==UART_STOP_BITS_1 && cfg->flow_ctrl==UART_HW_FLOWCTRL_DISABLE);return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts)
{ assert(tx==UART_PIN_NO_CHANGE && rts==UART_PIN_NO_CHANGE && cts==UART_PIN_NO_CHANGE);s_rx[port]=rx;return ESP_OK; }
esp_err_t uart_set_line_inverse(uart_port_t port, uint32_t flags)
{ assert(port==0 && flags==UART_SIGNAL_RXD_INV);s_inverse++;return ESP_OK; }
esp_err_t uart_flush_input(uart_port_t port) { (void)port;return ESP_OK; }
int uart_read_bytes(uart_port_t port, void *buffer, uint32_t size, TickType_t wait)
{ (void)port;(void)buffer;(void)size;(void)wait;return 0; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *task)
{ assert(fn==midi_in_task && stack==2048 && priority==5 && !task);assert((arg==&s_panel&&!strcmp(name,"midi_pnl"))||(arg==&s_din&&!strcmp(name,"midi_in")));s_tasks++;return pdPASS; }
void event_bus_publish(const evt_t *event)
{ assert(s_events<8);s_captured[s_events++]=*event; }
int main(void)
{
    midi_in_init(DIN_MIDI_RX_PIN);midi_panel_init(PANEL_MIDI_RX_PIN);
    assert(s_rx[0]==42 && s_rx[1]==16 && s_input_mask==(1ULL<<42) && !s_inverse && s_tasks==2);
    // Partial/running-status messages from the two physical ports stay independent.
    midi_parser_feed(&s_panel.parser,0x90);midi_parser_feed(&s_panel.parser,60);
    midi_parser_feed(&s_din.parser,0xB0);midi_parser_feed(&s_din.parser,7);midi_parser_feed(&s_din.parser,99);
    midi_parser_feed(&s_panel.parser,100);assert(s_events==2);
    assert(s_captured[0].midi_source==MIDI_SOURCE_DIN && s_captured[0].midi.status==0xB0);
    assert(s_captured[1].midi_source==MIDI_SOURCE_PANEL && s_captured[1].midi.status==0x90 && s_captured[1].midi.data[0]==60);
    midi_parser_feed(&s_panel.parser,61);midi_parser_feed(&s_panel.parser,110);
    assert(s_captured[2].midi_source==MIDI_SOURCE_PANEL && s_captured[2].midi.data[0]==61);
    s_idle=0;midi_panel_init(PANEL_MIDI_RX_PIN);assert(s_inverse==1 && s_rx[0]==42);
    puts("panel MIDI: UART0 GPIO42/31250 8N1, input-only preflight, inversion, DIN GPIO16 and independent parsers PASS");
    return 0;
}
