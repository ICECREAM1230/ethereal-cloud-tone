#include "b_runtime.h"
#include "platform.h"
#include "b_engine.h"
#include "b_console.h"
#include "b_router.h"
#include "b_storage.h"
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static b_engine_t engine;
static b_state_t state;
static harp_target_t published;
static harp_route_t published_route;
static uint32_t target_seq,seen_hits[12];
static bool have_target,gpio_ok,storage_ok;
static uint16_t last_raw;
static uint8_t last_ab[4]={3,3,3,3},last_pressed,last_enabled;
static uint32_t last_gap,max_gap,max_work,missed;

static void publish_target(bool force)
{
    harp_target_t next;harp_target(&engine.model,&next);
    harp_route_t route=(harp_route_t)engine.config.route;
    if(force || !have_target || route!=published_route || memcmp(&published,&next,sizeof(next))) {
        published=next;published_route=route;have_target=true;
        ++target_seq;b_router_publish(&next,route,target_seq);
    }
}

static void snapshot(uint32_t now)
{
    memset(&state,0,sizeof(state));b_engine_snapshot(&engine,&state);
    state.uptime_ms=now;state.raw=last_raw;state.encoder_enabled=last_enabled;
    for(unsigned i=0;i<4;i++) state.ab[i]=last_ab[i];
    state.pressed=last_pressed;
    state.timing.last_gap_us=last_gap;state.timing.max_gap_us=max_gap;
    state.timing.max_work_us=max_work;state.timing.missed_periods=missed;
    b_router_report_t router;b_router_snapshot(&router);
    state.links.usb=router.usb;state.links.ble=router.ble;state.links.selected=router.selected;
    state.links.usb_sent=router.usb_sent;state.links.ble_sent=router.ble_sent;
    state.links.usb_retries=router.usb_retries;state.links.ble_retries=router.ble_retries;
    state.links.ble_errors=router.ble_errors;
    b_console_drop_counts(&state.drops.commands,&state.drops.replies,&state.drops.events);
    state.heap_free=esp_get_free_heap_size();
}

static void reply(const b_reply_t *message,uint32_t now)
{
    if(!message->ready) return;
    snapshot(now);
    /* Copy the configuration AT acknowledgment, never read owner state later
     * from the slow UART task. The revision and payload remain one transaction. */
    b_console_reply(message,&engine.config,&state,engine.stream_period_ms);
}

static void collect_events(uint32_t now)
{
    for(unsigned i=0;i<12;i++) {
        uint32_t serial=engine.model.note_serial[i];
        if(serial==seen_hits[i]) continue;
        seen_hits[i]=serial;
        b_event_t event={.seq=serial,.time_ms=now,.channel=i+1,
            .duration_ms=engine.model.last_duration_ms[i],.velocity=engine.model.velocity[i],
            .note=harp_effective_note(&engine.model,i),.synthetic=engine.model.last_synthetic[i],
            .revision=engine.revision};
        b_console_event(&event);
    }
}

static void owner_task(void *arg)
{
    (void)arg;TickType_t next=xTaskGetTickCount();int64_t previous_sample=0;
    uint32_t last_snapshot=0;publish_target(true);
    for(;;) {
        int64_t started=esp_timer_get_time();uint32_t now=(uint32_t)(started/1000);
        b_reply_t response;
        b_storage_result_t stored;
        if(b_storage_poll(&stored) && engine.lifecycle==B_LIFECYCLE_SAVING &&
           stored.id==engine.pending_request.id) {
            b_engine_complete(&engine,stored.result==ESP_OK,&response);
            publish_target(true);reply(&response,now);
        }
        for(unsigned n=0;n<2;n++) {
            b_request_t request;if(!b_console_take(&request)) break;
            if(request.verb==B_VERB_SAVE && !storage_ok) {
                response=(b_reply_t){.ready=true,.include_config=true,.id=request.id,
                    .revision=engine.revision,.code=B_STORAGE_ERROR};
            } else {
                b_engine_command(&engine,&request,now,target_seq+1,&response);
                bool barrier=engine.lifecycle==B_LIFECYCLE_QUIESCING && engine.drain_target_seq==target_seq+1;
                publish_target(barrier);collect_events(now);
            }
            reply(&response,now);
        }
        b_router_report_t router;b_router_snapshot(&router);
        b_action_t action=b_engine_tick(&engine,now,router.processed_seq,router.quiet,&response);
        reply(&response,now);
        if(action==B_ACTION_RECONFIGURE) {
            bool success=true;
            if(engine.gpio_changed) {
                success=b_board_apply(engine.pending_config.pins)==ESP_OK;
                if(!success && b_board_apply(engine.config.pins)!=ESP_OK) gpio_ok=false;
            }
            b_engine_complete(&engine,success,&response);
            if(!gpio_ok) engine.lifecycle=B_LIFECYCLE_FAULT;
            publish_target(true);reply(&response,now);
        } else if(action==B_ACTION_SAVE && !b_storage_save(engine.pending_request.id,&engine.config)) {
            b_engine_complete(&engine,false,&response);publish_target(true);reply(&response,now);
        }
        if(gpio_ok && engine.lifecycle==B_LIFECYCLE_RUNNING) {
            int64_t sampled_at=esp_timer_get_time();
            if(previous_sample) {
                uint64_t gap=(uint64_t)(sampled_at-previous_sample);
                last_gap=(uint32_t)(gap>UINT32_MAX?UINT32_MAX:gap);
                if(last_gap>max_gap) max_gap=last_gap;
                if(last_gap>=2000) missed+=last_gap/1000-1;
            }
            previous_sample=sampled_at;
            last_enabled=b_board_read(&last_raw,last_ab,&last_pressed);
            b_engine_sample(&engine,last_raw,last_ab,last_pressed,last_enabled,(uint32_t)(sampled_at/1000));
            collect_events((uint32_t)(sampled_at/1000));publish_target(false);
        } else previous_sample=0;
        if((uint32_t)(now-last_snapshot)>=20) {
            last_snapshot=now;snapshot(now);b_console_state(&state);
        }
        uint32_t work=(uint32_t)(esp_timer_get_time()-started);if(work>max_work) max_work=work;
        /* Skip missed slots rather than executing bursts of fake catch-up
         * samples after a pause; input timestamps retain actual elapsed time. */
        TickType_t tick=xTaskGetTickCount();
        if((TickType_t)(tick-next)>=pdMS_TO_TICKS(1)) next=tick;
        vTaskDelayUntil(&next,pdMS_TO_TICKS(1));
    }
}

static void build_defaults(b_config_t *c)
{
    b_config_defaults(c);c->channel=CONFIG_B_MIDI_CHANNEL;
    for(unsigned i=0;i<12;i++) c->notes[i]=CONFIG_B_FIRST_NOTE+(int)i;
    c->velocity[0]=CONFIG_B_DURATION_MIN_MS;c->velocity[1]=CONFIG_B_DURATION_MAX_MS;
    c->velocity[2]=CONFIG_B_VELOCITY_MIN;c->velocity[3]=CONFIG_B_VELOCITY_MAX;
    c->gate=CONFIG_B_NOTE_GATE_MS;c->polarity=CONFIG_B_LASER_ACTIVE_LEVEL;
    c->enc_edges=CONFIG_B_ENCODER_EDGES;c->enc_reverse=CONFIG_B_ENCODER_REVERSE_MASK;
    c->route=CONFIG_B_OUTPUT_MODE;b_board_defaults(c->pins);
    if(b_board_validate(c->pins)!=ESP_OK)
        for(unsigned i=0;i<4;i++) for(unsigned p=0;p<3;p++) c->pins[i][p]=-1;
    if(!b_config_valid(c)) {
        ESP_LOGW("b_init","Invalid build defaults; known B defaults retained");b_config_defaults(c);
    }
}

esp_err_t b_runtime_start(void)
{
    esp_err_t rc=b_console_start();if(rc!=ESP_OK) return rc;
    b_config_t defaults,loaded;bool migrated;
    build_defaults(&defaults);
    rc=b_storage_start(&defaults,&loaded,&storage_ok,&migrated);if(rc!=ESP_OK) return rc;
    if(!b_engine_init(&engine,&loaded)) return ESP_ERR_INVALID_ARG;
    rc=b_board_init(loaded.pins);gpio_ok=rc==ESP_OK;
    if(!gpio_ok) { ESP_LOGE("b_init","GPIO: %s",esp_err_to_name(rc));engine.lifecycle=B_LIFECYCLE_FAULT; }
    esp_err_t usb=b_usb_init(),ble=b_ble_init();
    if(usb!=ESP_OK) ESP_LOGE("b_init","USB unavailable: %s",esp_err_to_name(usb));
    if(ble!=ESP_OK) ESP_LOGE("b_init","BLE unavailable: %s",esp_err_to_name(ble));
    rc=b_router_start(usb==ESP_OK,ble==ESP_OK);if(rc!=ESP_OK) return rc;
    snapshot((uint32_t)(esp_timer_get_time()/1000));b_console_state(&state);
    ESP_LOGI("b_init","MIDI engine channel ID=%"PRId32" (channel %"PRId32"); shared by all beams and encoder controls on USB/BLE",loaded.channel-1,loaded.channel);
    ESP_LOGI("b_init","Shade velocity: %"PRId32" ms -> %"PRId32", %"PRId32" ms -> %"PRId32,loaded.velocity[0],loaded.velocity[2],loaded.velocity[1],loaded.velocity[3]);
    ESP_LOGI("b_init","UART debug: harp channel [0..15], harp get, harp save");
    ESP_LOGI("b_init","B input owner=8, MIDI=5, UART RX=3/TX=2, storage=2; NVS=%u migrated=%u",storage_ok,migrated);
    return xTaskCreate(owner_task,"b_input",8192,NULL,8,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
