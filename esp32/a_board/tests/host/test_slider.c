#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "slider.c"

static jmp_buf s_stop;
static unsigned s_sample_count, s_sample_index, s_events, s_nvs_init, s_commits;
static unsigned s_tasks, s_adc_gpio, s_usb_installs, s_console_count, s_console_index, s_ble_status;
static int s_samples[1024];
static uint8_t s_values[128], s_console[8];
static uint16_t s_saved_min, s_saved_max;
static uint64_t s_period;

const char *esp_err_to_name(esp_err_t error) { (void)error; return "stub"; }
esp_err_t nvs_flash_init(void) { s_nvs_init++; return ESP_OK; }
esp_err_t nvs_flash_erase(void) { assert(0); return ESP_FAIL; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ assert(!strcmp(name,"panel")); *handle=1; return mode==NVS_READONLY ? ESP_FAIL:ESP_OK; }
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{ (void)handle; (void)key; (void)value; return ESP_FAIL; }
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{ assert(handle==1); if(!strcmp(key,"end_min"))s_saved_min=value; else {assert(!strcmp(key,"end_max"));s_saved_max=value;} return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle==1); s_commits++; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { assert(handle==1); }
esp_err_t adc_oneshot_io_to_channel(int gpio, adc_unit_t *unit, adc_channel_t *channel)
{ assert(gpio==8); s_adc_gpio=gpio; *unit=ADC_UNIT_1; *channel=7; return ESP_OK; }
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg, adc_oneshot_unit_handle_t *adc)
{ assert(cfg->unit_id==ADC_UNIT_1); *adc=(void *)1; return ESP_OK; }
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t adc, adc_channel_t channel, const adc_oneshot_chan_cfg_t *cfg)
{ assert(adc==(void *)1 && channel==7 && cfg->atten==ADC_ATTEN_DB_12); return ESP_OK; }
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t adc, adc_channel_t channel, int *sample)
{ assert(adc==(void *)1 && channel==7 && s_sample_index<s_sample_count); *sample=s_samples[s_sample_index++]; return *sample<0 ? ESP_FAIL:ESP_OK; }
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *cfg)
{ assert(cfg->rx_buffer_size==256 && cfg->tx_buffer_size==256); s_usb_installs++; return ESP_OK; }
int usb_serial_jtag_read_bytes(void *buffer, unsigned size, TickType_t wait)
{ assert(size==1 && wait==portMAX_DELAY); if(s_console_index==s_console_count)longjmp(s_stop,1); *(uint8_t *)buffer=s_console[s_console_index++]; return 1; }
void ble_midi_console_status(void) { s_ble_status++; }
int64_t esp_timer_get_time(void) { return 0; }
int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{ assert(!strcmp(args->name,"slider_poll")); *timer=(void *)1; return ESP_OK; }
int esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{ assert(timer==(void *)1); s_period=period; return ESP_OK; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *task)
{ assert(stack==3072 && !arg); if(!strcmp(name,"slider")){assert(fn==slider_task && priority==4);*task=(void *)1;}else{assert(!strcmp(name,"slider_con") && fn==console_task && priority==3);}s_tasks++;return pdPASS; }
void xTaskNotifyGive(TaskHandle_t task) { assert(task==(void *)1); }
unsigned ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{ assert(clear==pdTRUE && wait==portMAX_DELAY); if(s_sample_index==s_sample_count)longjmp(s_stop,1); return 1; }
void event_bus_publish(const evt_t *event)
{ assert(event->kind==EVT_MIDI && event->midi.status==0xB0 && event->midi.len==2 && event->midi.data[0]==71); assert(s_events<128);s_values[s_events++]=event->midi.data[1]; }

static void samples(unsigned count, int raw)
{ assert(count<=1024);s_sample_count=count;s_sample_index=0;for(unsigned i=0;i<count;i++)s_samples[i]=raw; }
static void poll(void) { if(!setjmp(s_stop))slider_task(NULL); }
static void key(uint8_t ch)
{ s_console[0]=ch;s_console_count=1;s_console_index=0;if(!setjmp(s_stop))console_task(NULL); }
int main(void)
{
    slider_init();
    assert(s_adc_gpio==8 && s_adc_channel==7 && s_nvs_init==1 && s_usb_installs==1);
    assert(s_tasks==2 && s_period==2000 && s_slider_active);
    samples(40,2130);for(unsigned i=16;i<40;i++)s_samples[i]=2130+(i%2 ? 15:-15);
    poll();assert(s_events==1 && s_values[0]==64); // settled midpoint/noise stays silent
    samples(30,4000);poll();assert(s_values[s_events-1]==127); // rail still reaches exact full scale
    samples(30,260);poll();assert(s_values[s_events-1]==0);
    unsigned events=s_events;samples(12,-1);poll();assert(s_events==events); // failed ADC read never invents CC
    key('1');assert(s_capture_end==1);samples(500,300);poll();
    assert(!s_capture_end && s_saved_min==300 && s_commits==1);
    key('2');assert(s_capture_end==2);samples(500,3900);poll();
    assert(!s_capture_end && s_saved_max==3900 && s_commits==2 && s_raw_min==300 && s_raw_max==3900);
    key('p');assert(s_ble_status==1); // shared console survives the restored ADC
    puts("slider: GPIO8/ADC1_CH7, 500Hz, real CC71, noise/rails/errors, two-end NVS calibration and USB console PASS");
    return 0;
}
