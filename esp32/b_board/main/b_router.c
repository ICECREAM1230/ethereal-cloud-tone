#include "b_router.h"
#include <string.h>
typedef struct { harp_target_t target;harp_route_t route;uint32_t seq; } target_message_t;
static QueueHandle_t targets,reports;
static bool usb_ok,ble_ok;
static void reset_channel(harp_sink_t *s)
{
    uint32_t sent=s->sent,retries=s->retries;
    harp_sink_init(s);s->sent=sent;s->retries=retries;
}
static void router_task(void *arg)
{
    (void)arg;static target_message_t latest;static harp_sink_t sinks[2];
    bool have_target=false;uint8_t previous_channel=0;
    harp_sink_init(&sinks[0]);harp_sink_init(&sinks[1]);
    for(;;) {
        if(xQueueReceive(targets,&latest,pdMS_TO_TICKS(1))==pdTRUE) {
            if(have_target && previous_channel!=latest.target.channel) {
                /* Old channel is drained by the engine barrier. Its parameter
                 * shadow must also be reset before initializing a new channel. */
                reset_channel(&sinks[0]);reset_channel(&sinks[1]);
            }
            previous_channel=latest.target.channel;have_target=true;
        }
        uint32_t epoch[2]={0};
        bool ready[2]={usb_ok && b_usb_ready(&epoch[0]),ble_ok && b_ble_ready(&epoch[1])};
        unsigned selected=have_target?harp_route_mask(latest.route,ready[0],ready[1]):0;
        const harp_send_fn send[2]={b_usb_send,b_ble_send};
        if(have_target) {
            bool old_quiet=true;
            for(unsigned i=0;i<2;i++) if(!(selected&(1u<<i))) {
                harp_sink_pump(&sinks[i],&latest.target,false,ready[i],epoch[i],8,send[i],NULL);
                old_quiet=old_quiet && harp_sink_quiet(&sinks[i]);
            }
            if(old_quiet) for(unsigned i=0;i<2;i++) if(selected&(1u<<i))
                harp_sink_pump(&sinks[i],&latest.target,true,ready[i],epoch[i],8,send[i],NULL);
        }
        if(usb_ok) b_usb_drain();
        b_router_report_t report={.processed_seq=have_target?latest.seq:0,
            .quiet=harp_sink_quiet(&sinks[0]) && harp_sink_quiet(&sinks[1]),
            .usb=ready[0],.ble=ready[1],.selected=selected,
            .usb_sent=sinks[0].sent,.ble_sent=sinks[1].sent,
            .usb_retries=sinks[0].retries,.ble_retries=sinks[1].retries,
            .ble_errors=ble_ok?b_ble_write_errors():0};
        xQueueOverwrite(reports,&report);
    }
}
esp_err_t b_router_start(bool usb_initialized,bool ble_initialized)
{
    targets=xQueueCreate(1,sizeof(target_message_t));reports=xQueueCreate(1,sizeof(b_router_report_t));
    if(!targets || !reports) return ESP_ERR_NO_MEM;
    usb_ok=usb_initialized;ble_ok=ble_initialized;
    return xTaskCreate(router_task,"b_midi",4096,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
void b_router_publish(const harp_target_t *target,harp_route_t route,uint32_t seq)
{
    target_message_t message={.target=*target,.route=route,.seq=seq};
    if(targets) xQueueOverwrite(targets,&message);
}
void b_router_snapshot(b_router_report_t *out)
{ memset(out,0,sizeof(*out));if(reports) xQueuePeek(reports,out,0); }
