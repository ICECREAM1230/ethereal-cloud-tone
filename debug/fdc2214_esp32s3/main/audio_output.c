#include "audio_output.h"
#include "volume_audio.h"
#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct { fdc_frame_t frame; uint32_t epoch; bool fault; } input_t;
typedef struct {
    vc_status_t status;
    uint64_t updated_us;
    esp_err_t driver_error;
    unsigned last_command;
    bool command_ok;
} cache_t;
static QueueHandle_t inputs,commands;
static SemaphoreHandle_t lock;
static cache_t cache;
static vc_state_t controller;
static i2s_chan_handle_t tx_channel;
static bool ready;
static esp_err_t start_error=ESP_ERR_INVALID_STATE;
/* Written only by the existing acquisition owner, carried inside each queue item. */
static uint32_t source_epoch;

static cache_t read_cache(void)
{
    cache_t copy;
    xSemaphoreTake(lock,portMAX_DELAY);copy=cache;xSemaphoreGive(lock);return copy;
}

static void calibration_task(void *argument)
{
    (void)argument;ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    TickType_t wake=xTaskGetTickCount(),period=pdMS_TO_TICKS(10);
    if (!period) period=1;
    uint32_t last_epoch=0;bool have_epoch=false;
    for (;;) {
        uint64_t now=(uint64_t)esp_timer_get_time();
        vc_command_t command;unsigned last=0;bool command_ok=true,had_command=false;
        while (xQueueReceive(commands,&command,0)==pdTRUE) {
            command_ok=vc_execute(&controller,&command,now);last=(unsigned)command.kind;had_command=true;
        }
        input_t input;vc_sample_t sample,*new_sample=NULL;
        if (xQueueReceive(inputs,&input,0)==pdTRUE) {
            unsigned ch=controller.config.channel;
            sample=(vc_sample_t){.time_us=input.frame.time_us,.raw=input.frame.raw[ch],.flags=input.frame.flags[ch],
                .hard_error=input.fault || (input.frame.status_errors&0x0800u) || (input.frame.ratio_error_mask&(1u<<ch))};
            if (have_epoch && input.epoch!=last_epoch) sample.hard_error=true;
            last_epoch=input.epoch;have_epoch=true;
            /* STATUS error attribution can span scans: retain uncertainty as AW. */
            if (input.frame.status_errors&0x0600u) sample.flags|=1;
            new_sample=&sample;
        }
        (void)vc_update(&controller,now,new_sample);
        vc_status_t status=vc_status(&controller,now);
        xSemaphoreTake(lock,portMAX_DELAY);
        cache.status=status;cache.updated_us=now;
        if (had_command) {cache.last_command=last;cache.command_ok=command_ok;}
        xSemaphoreGive(lock);
        vTaskDelayUntil(&wake,period);
    }
}

static void audio_task(void *argument)
{
    (void)argument;ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    int16_t samples[240*2];vc_oscillator_t oscillator={0};
    for (;;) {
        cache_t current=read_cache();
        uint64_t now=(uint64_t)esp_timer_get_time();
        float gain=current.status.signal_gain;
        if (!current.updated_us || now-current.updated_us>500000) gain=0;
        if (!vc_render(&oscillator,samples,240,VC_SAMPLE_RATE,current.status.config.tone_hz,gain)) memset(samples,0,sizeof(samples));
        size_t written=0;
        esp_err_t error=i2s_channel_write(tx_channel,samples,sizeof(samples),&written,100);
        if (error!=ESP_OK || written!=sizeof(samples)) {
            if (error==ESP_OK) error=ESP_FAIL;
            xSemaphoreTake(lock,portMAX_DELAY);cache.driver_error=error;xSemaphoreGive(lock);
            (void)i2s_channel_disable(tx_channel);
            vTaskDelete(NULL);return;
        }
    }
}

static void cleanup_start(void)
{
    if (tx_channel) { (void)i2s_del_channel(tx_channel);tx_channel=NULL; }
    if (inputs) {vQueueDelete(inputs);inputs=NULL;}
    if (commands) {vQueueDelete(commands);commands=NULL;}
    if (lock) {vSemaphoreDelete(lock);lock=NULL;}
}

esp_err_t audio_output_start(const fdc_config_t *fdc)
{
    if (!fdc || ready) return ESP_ERR_INVALID_STATE;
    int pins[]={CONFIG_FDC_AUDIO_MCLK_GPIO,CONFIG_FDC_AUDIO_BCLK_GPIO,CONFIG_FDC_AUDIO_WS_GPIO,CONFIG_FDC_AUDIO_DOUT_GPIO};
    int fdc_pins[]={fdc->sda_gpio,fdc->scl_gpio,fdc->sd_gpio,fdc->addr_gpio};
    int uart_tx=-1,uart_rx=-1;
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
    uart_tx=43;uart_rx=44;
#elif defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    uart_tx=CONFIG_ESP_CONSOLE_UART_TX_GPIO;uart_rx=CONFIG_ESP_CONSOLE_UART_RX_GPIO;
#endif
    if (!vc_s3_pins_valid(pins,fdc_pins,uart_tx,uart_rx)) return start_error=ESP_ERR_INVALID_ARG;
    for (unsigned i=0;i<4;i++) if (pins[i]>=0 && !GPIO_IS_VALID_OUTPUT_GPIO(pins[i])) return start_error=ESP_ERR_INVALID_ARG;
    lock=xSemaphoreCreateMutex();inputs=xQueueCreate(1,sizeof(input_t));commands=xQueueCreate(8,sizeof(vc_command_t));
    if (!lock || !inputs || !commands) {cleanup_start();return start_error=ESP_ERR_NO_MEM;}
    vc_config_t config=vc_default_config();
    if (!vc_init(&controller,&config)) {cleanup_start();return start_error=ESP_ERR_INVALID_ARG;}
    cache=(cache_t){.status=vc_status(&controller,(uint64_t)esp_timer_get_time()),.command_ok=true};
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,I2S_ROLE_MASTER);
    channel.dma_desc_num=6;channel.dma_frame_num=240;channel.auto_clear_after_cb=true;
    esp_err_t error=i2s_new_channel(&channel,&tx_channel,NULL);
    if (error!=ESP_OK) {cleanup_start();return start_error=error;}
    i2s_std_config_t standard={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(VC_SAMPLE_RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=pins[0],.bclk=pins[1],.ws=pins[2],.dout=pins[3],.din=I2S_GPIO_UNUSED,
            .invert_flags={.mclk_inv=false,.bclk_inv=false,.ws_inv=false}}};
    standard.slot_cfg.slot_bit_width=I2S_SLOT_BIT_WIDTH_32BIT;
    standard.slot_cfg.ws_width=32; /* Keep Philips WS at50% after overriding slot width. */
    error=i2s_channel_init_std_mode(tx_channel,&standard);
    if (error!=ESP_OK) {cleanup_start();return start_error=error;}
    int16_t silence[240*2]={0};
    for (unsigned i=0;i<6;i++) {
        size_t loaded=0;error=i2s_channel_preload_data(tx_channel,silence,sizeof(silence),&loaded);
        if (error!=ESP_OK) {cleanup_start();return start_error=error;}
        if (!loaded) break;
    }
    TaskHandle_t audio=NULL,calibration=NULL;
    if (xTaskCreate(audio_task,"fdc_i2s",4096,NULL,4,&audio)!=pdPASS) {cleanup_start();return start_error=ESP_ERR_NO_MEM;}
    if (xTaskCreate(calibration_task,"fdc_volume",4096,NULL,3,&calibration)!=pdPASS) {
        vTaskDelete(audio);cleanup_start();return start_error=ESP_ERR_NO_MEM;
    }
    error=i2s_channel_enable(tx_channel);
    if (error!=ESP_OK) {vTaskDelete(audio);vTaskDelete(calibration);cleanup_start();return start_error=error;}
    ready=true;start_error=ESP_OK;xTaskNotifyGive(calibration);xTaskNotifyGive(audio);
    return ESP_OK;
}

void audio_output_submit(const fdc_frame_t *frame)
{
    if (!ready || !frame) return;
    input_t input={.frame=*frame,.epoch=source_epoch};(void)xQueueOverwrite(inputs,&input);
}
void audio_output_fault(void)
{
    if (!ready) return;
    source_epoch++;
    input_t input={.fault=true,.epoch=source_epoch};(void)xQueueOverwrite(inputs,&input);
}

void audio_output_log(audio_print_t print,bool full)
{
    if (!ready) {if (full) print("# audio unavailable: %s",esp_err_to_name(start_error));return;}
    cache_t c=read_cache();const vc_status_t *s=&c.status;
    if (full) {
        print("# audio error=%s mode=%u level=%u tone=%u fs=48000 sample_bits=16 slot_bits=32 MCLK=%d BCLK=%d LRCLK=%d DOUT=%d",
            esp_err_to_name(c.driver_error),(unsigned)s->mode,(unsigned)s->config.level_percent,(unsigned)s->config.tone_hz,
            CONFIG_FDC_AUDIO_MCLK_GPIO,CONFIG_FDC_AUDIO_BCLK_GPIO,CONFIG_FDC_AUDIO_WS_GPIO,CONFIG_FDC_AUDIO_DOUT_GPIO);
        print("# volume loud=%"PRIu32" mute=%"PRIu32" curve=%u calibrated=%u pending_loud=%"PRIu32" pending_mute=%"PRIu32" pending_mask=%u last_command=%u command_ok=%u",
            s->config.raw_loud,s->config.raw_mute,(unsigned)s->config.curve,s->calibrated,s->pending_loud,s->pending_mute,
            (unsigned)s->pending_mask,c.last_command,c.command_ok);
    }
    print("volume,%"PRIu64",%u,%"PRIu32",%.6f,%"PRIu32",%u,%.6f,%u,%s,%u,%u,%u,%u,0x%x",
        c.updated_us,(unsigned)s->config.channel,s->raw,(double)s->position,s->gain_q16,
        (unsigned)s->config.level_percent,(double)s->signal_gain,(unsigned)s->mode,vc_quality_name(s->quality),
        (unsigned)s->capture_result,(unsigned)s->capture_count,(unsigned)s->capture_aw,s->calibrated,(unsigned)c.driver_error);
}

int audio_output_command(int argc,char **argv,audio_print_t print)
{
    vc_command_t command;
    if (!vc_parse_command(argc,(const char *const*)argv,&command)) {
        print("# audio show|off|reference|sensor|level 0..100|tone 20..2000");
        print("# vol show|channel 0..3|capture loud|capture mute|range LOUD MUTE|curve linear|curve square|apply|export");return 1;
    }
    if (!ready) {audio_output_log(print,true);return 1;}
    if (command.kind==VC_SHOW) {audio_output_log(print,true);return 0;}
    if (command.kind==VC_EXPORT) {
        cache_t c=read_cache();
        print("# cfg.volume.raw_far=%"PRIu32"; cfg.volume.raw_near=%"PRIu32"; cfg.volume.noise_sigma=%.4f;",
            c.status.config.raw_loud,c.status.config.raw_mute,(double)c.status.config.noise_sigma);
        print("# channel=%u curve=%u calibrated=%u; level=%u is TEST AUDIO ONLY; parameters are RAM-only",
            (unsigned)c.status.config.channel,(unsigned)c.status.config.curve,c.status.calibrated,(unsigned)c.status.config.level_percent);return 0;
    }
    if (xQueueSend(commands,&command,0)!=pdTRUE) {print("# volume command queue full; retry");return 1;}
    print("# command queued; vol show reports command_ok and pending capture state");return 0;
}
