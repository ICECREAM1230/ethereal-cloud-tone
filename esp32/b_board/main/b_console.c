#include "b_console.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
typedef struct { b_reply_t reply;b_config_t config;b_state_t state;uint32_t period; } reply_message_t;
static QueueHandle_t commands,replies,states,events;
static portMUX_TYPE drop_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t command_drops,reply_drops,event_drops;
static void increment(uint32_t *counter)
{ portENTER_CRITICAL(&drop_lock);++*counter;portEXIT_CRITICAL(&drop_lock); }
static void reject(const b_request_t *request,b_code_t code)
{
    static b_state_t snapshot;
    memset(&snapshot,0,sizeof(snapshot));xQueuePeek(states,&snapshot,0);
    b_reply_t reply={.ready=true,.id=request->id,.revision=snapshot.revision,.code=code};
    b_console_reply(&reply,NULL,&snapshot,UINT32_MAX);increment(&command_drops);
}
static void rx_task(void *arg)
{
    (void)arg;char line[B_PROTOCOL_MAX_LINE+1];size_t used=0;bool discard=false;uint8_t bytes[128];
    for(;;) {
        int count=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),pdMS_TO_TICKS(20));
        for(int i=0;i<count;i++) {
            uint8_t c=bytes[i];
            if(c=='\n' || c=='\r') {
                if(!used && !discard) continue;
                b_request_t request;b_code_t code=b_protocol_parse_line(line,used,&request);
                if(discard) reject(&request,B_BAD_REQUEST);
                else if(code!=B_OK) reject(&request,code);
                else if(xQueueSend(commands,&request,0)!=pdTRUE) reject(&request,B_QUEUE_FULL);
                used=0;discard=false;
            } else if(!discard) {
                if(c==8 || c==127) { if(used) --used; }
                else if(c=='\t' || (c>=32 && c<127)) {
                    if(used==B_PROTOCOL_MAX_LINE) discard=true;else line[used++]=(char)c;
                } else discard=true;
            }
        }
    }
}
static void tx_task(void *arg)
{
    (void)arg;static reply_message_t message;static b_state_t snapshot;static char frame[B_PROTOCOL_FRAME_CAP];
    uint32_t period=200,last_stream=0;
    for(;;) {
        if(xQueueReceive(replies,&message,pdMS_TO_TICKS(10))==pdTRUE) {
            if(message.period!=UINT32_MAX) period=message.period;
            size_t n=b_protocol_format_reply(frame,sizeof(frame),&message.reply,&message.config,&message.state);
            if(n) uart_write_bytes(UART_NUM_0,frame,n);else increment(&reply_drops);
        }
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        if(period && (uint32_t)(now-last_stream)>=period && xQueuePeek(states,&snapshot,0)==pdTRUE) {
            last_stream=now;snapshot.event_count=0;
            while(snapshot.event_count<B_PROTOCOL_MAX_EVENTS &&
                  xQueueReceive(events,&snapshot.events[snapshot.event_count],0)==pdTRUE) ++snapshot.event_count;
            size_t n=b_protocol_format_state(frame,sizeof(frame),&snapshot);
            if(n) uart_write_bytes(UART_NUM_0,frame,n);else increment(&reply_drops);
        }
    }
}
esp_err_t b_console_start(void)
{
    commands=xQueueCreate(16,sizeof(b_request_t));replies=xQueueCreate(8,sizeof(reply_message_t));
    states=xQueueCreate(1,sizeof(b_state_t));events=xQueueCreate(64,sizeof(b_event_t));
    if(!commands || !replies || !states || !events) return ESP_ERR_NO_MEM;
    const uart_config_t cfg={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t rc=uart_param_config(UART_NUM_0,&cfg);if(rc!=ESP_OK) return rc;
    rc=uart_set_pin(UART_NUM_0,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);if(rc!=ESP_OK) return rc;
    rc=uart_driver_install(UART_NUM_0,4096,4096,0,NULL,0);if(rc!=ESP_OK) return rc;
    uart_vfs_dev_use_driver(UART_NUM_0);
    if(xTaskCreate(rx_task,"b_uart_rx",4096,NULL,3,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    return xTaskCreate(tx_task,"b_uart_tx",6144,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
bool b_console_take(b_request_t *request)
{ return commands && xQueueReceive(commands,request,0)==pdTRUE; }
void b_console_reply(const b_reply_t *reply,const b_config_t *config,const b_state_t *state,uint32_t period)
{
    if(!reply || !reply->ready || !replies) return;
    reply_message_t message={.reply=*reply,.period=period};
    if(config) message.config=*config;
    if(state) message.state=*state;
    if(xQueueSend(replies,&message,0)!=pdTRUE) increment(&reply_drops);
}
void b_console_state(const b_state_t *state)
{ if(states) xQueueOverwrite(states,state); }
void b_console_event(const b_event_t *event)
{ if(events && xQueueSend(events,event,0)!=pdTRUE) increment(&event_drops); }
void b_console_drop_counts(uint32_t *cmd,uint32_t *reply,uint32_t *event)
{
    portENTER_CRITICAL(&drop_lock);*cmd=command_drops;*reply=reply_drops;*event=event_drops;portEXIT_CRITICAL(&drop_lock);
}
