#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fdc2214.h"
#include "fdc_ranges.h"
#if CONFIG_FDC_AUDIO_ENABLE
#include "audio_output.h"
#endif

enum { REQUEST_CLEAR=1u, REQUEST_RESTART=2u };
typedef enum { STATE_UNCONFIGURED, STATE_STARTING, STATE_RUNNING, STATE_FAULT } state_t;
typedef struct {
    state_t state;
    char reason[112];
    fdc_frame_t frame;
    fdc_ranges_t ranges;
    uint64_t sequence;
    uint64_t output_dropped_lines;
    bool stream;
    uint8_t last_reg;
    uint16_t expected,actual;
    int error;
} snapshot_t;

static struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
} hardware;
static SemaphoreHandle_t state_lock;
static TaskHandle_t acquisition_handle;
static snapshot_t shared;
static fdc_config_t configuration;

// Format one complete application record, separate from the capture owner.
// USB driver mutex/ring-buffer waits each receive a 20 ms tick timeout.
// stdio contention drops this record immediately. Capture never waits on I/O.
static void record_line(const char *format,...)
{
    char line[384];va_list arguments;va_start(arguments,format);
    int length=vsnprintf(line,sizeof(line)-1,format,arguments);va_end(arguments);
    bool dropped=length<0 || (size_t)length>=sizeof(line)-1;
    if (!dropped) {
        line[length++]='\n';
        if (ftrylockfile(stdout)!=0) dropped=true;
        else {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
            int written=usb_serial_jtag_write_bytes(line,(size_t)length,pdMS_TO_TICKS(20));
            dropped=written!=length;
            if (dropped) (void)usb_serial_jtag_write_bytes("\n",1,0);
#else
            dropped=fwrite(line,1,(size_t)length,stdout)!=(size_t)length;
#endif
            funlockfile(stdout);
        }
    }
    if (dropped) {
        xSemaphoreTake(state_lock,portMAX_DELAY);shared.output_dropped_lines++;xSemaphoreGive(state_lock);
    }
}

static const char *state_name(state_t state)
{
    switch (state) {
    case STATE_UNCONFIGURED:return "UNCONFIGURED";
    case STATE_STARTING:return "STARTING";
    case STATE_RUNNING:return "RUNNING";
    default:return "FAULT";
    }
}
static snapshot_t snapshot(void)
{
    snapshot_t copy;
    xSemaphoreTake(state_lock,portMAX_DELAY);copy=shared;xSemaphoreGive(state_lock);
    return copy;
}
static void publish_state(state_t state,const char *reason,const fdc_device_t *device,int error)
{
#if CONFIG_FDC_AUDIO_ENABLE
    if (state!=STATE_RUNNING) audio_output_fault();
#endif
    xSemaphoreTake(state_lock,portMAX_DELAY);
    shared.state=state;snprintf(shared.reason,sizeof(shared.reason),"%s",reason);
    shared.error=error;
    if (state!=STATE_RUNNING) shared.frame.valid_mask=0;
    if (device) {shared.last_reg=device->last_reg;shared.expected=device->expected;shared.actual=device->actual;}
    xSemaphoreGive(state_lock);
}

static int read_word(void *context,uint8_t reg,uint16_t *word)
{
    (void)context;
    uint8_t bytes[2]={0};
    esp_err_t result=i2c_master_transmit_receive(hardware.device,&reg,1,bytes,2,CONFIG_FDC_TRANSACTION_TIMEOUT_MS);
    if (result==ESP_OK) *word=fdc_be16(bytes);
    return result;
}
static int write_word(void *context,uint8_t reg,uint16_t word)
{
    (void)context;
    const uint8_t bytes[]={reg,(uint8_t)(word>>8),(uint8_t)word};
    return i2c_master_transmit(hardware.device,bytes,sizeof(bytes),CONFIG_FDC_TRANSACTION_TIMEOUT_MS);
}
static uint64_t now_us(void *context) {(void)context;return (uint64_t)esp_timer_get_time();}

static bool validate_before_hardware(char *reason,size_t size)
{
    if (!fdc_config_valid(&configuration,reason,size)) return false;
    const int pins[]={configuration.sda_gpio,configuration.scl_gpio,configuration.sd_gpio,configuration.addr_gpio};
    for (unsigned i=0;i<4;i++) {
        if (pins[i]<0) continue;
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pins[i])) {
            snprintf(reason,size,"GPIO%d is not a valid ESP32-S3 output",pins[i]);return false;
        }
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
        if (pins[i]==43 || pins[i]==44) {
            snprintf(reason,size,"GPIO%d overlaps ESP32-S3 default UART0",pins[i]);return false;
        }
#elif defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
        if (pins[i]==CONFIG_ESP_CONSOLE_UART_TX_GPIO || pins[i]==CONFIG_ESP_CONSOLE_UART_RX_GPIO) {
            snprintf(reason,size,"GPIO%d overlaps console UART",pins[i]);return false;
        }
#endif
    }
    return true;
}

static esp_err_t configure_output(int pin,int level)
{
    esp_err_t r=gpio_set_level((gpio_num_t)pin,level);
    if (r!=ESP_OK) return r;
    const gpio_config_t config={.pin_bit_mask=UINT64_C(1)<<pin,.mode=GPIO_MODE_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    return gpio_config(&config);
}
static void close_bus(void)
{
    if (hardware.device) {i2c_master_bus_rm_device(hardware.device);hardware.device=NULL;}
    if (hardware.bus) {i2c_del_master_bus(hardware.bus);hardware.bus=NULL;}
}
// Only the acquisition owner calls this, after the complete configuration gate.
static esp_err_t open_hardware(void)
{
    close_bus();
    esp_err_t r;
    if (configuration.sd_gpio>=0) {
        if ((r=configure_output(configuration.sd_gpio,1))!=ESP_OK) return r;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (configuration.addr_gpio>=0 && (r=configure_output(configuration.addr_gpio,configuration.address&1u))!=ESP_OK) return r;
    if (configuration.sd_gpio>=0 && (r=gpio_set_level((gpio_num_t)configuration.sd_gpio,0))!=ESP_OK) return r;
    // TI specifies at least 2 ms from SD falling to I2C access. A two-tick
    // delay can be shorter than 2 ms depending on tick phase; allow margin.
    vTaskDelay(pdMS_TO_TICKS(5));
    const i2c_master_bus_config_t bus={.i2c_port=I2C_NUM_0,
        .sda_io_num=(gpio_num_t)configuration.sda_gpio,.scl_io_num=(gpio_num_t)configuration.scl_gpio,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=false};
    if ((r=i2c_new_master_bus(&bus,&hardware.bus))!=ESP_OK) return r;
    const i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=configuration.address,
        .scl_speed_hz=CONFIG_FDC_I2C_HZ,.scl_wait_us=10000};
    r=i2c_master_bus_add_device(hardware.bus,&device,&hardware.device);
    if (r!=ESP_OK) close_bus();
    return r;
}

static void acquisition_task(void *argument)
{
    (void)argument;
    fdc_device_t device={0};
    const fdc_io_t io={.read=read_word,.write=write_word,.now_us=now_us};
    bool restart=true;
    for (;;) {
        uint32_t requests=0;
        xTaskNotifyWait(0,UINT32_MAX,&requests,0);
        if (requests&REQUEST_CLEAR) {
            xSemaphoreTake(state_lock,portMAX_DELAY);
            fdc_ranges_clear(&shared.ranges);memset(&shared.frame,0,sizeof(shared.frame));
            xSemaphoreGive(state_lock);
        }
        if (requests&REQUEST_RESTART) restart=true;
        if (restart) {
            restart=false;
            char reason[112];
            // No gpio_* or i2c_* call occurs on the unconfigured path.
            if (!validate_before_hardware(reason,sizeof(reason))) {
                publish_state(STATE_UNCONFIGURED,reason,NULL,0);
                vTaskDelay(pdMS_TO_TICKS(20));continue;
            }
            publish_state(STATE_STARTING,"initializing all four channels",NULL,0);
            esp_err_t result=open_hardware();
            if (result!=ESP_OK) {
                publish_state(STATE_FAULT,"GPIO/I2C initialization failed",NULL,result);
                vTaskDelay(pdMS_TO_TICKS(20));continue;
            }
            fdc_result_t f=fdc_init(&device,&configuration,&io);
            if (f!=FDC_OK) {
                // A failed ID read is not evidence of a bad converter. Probe
                // both legal addresses and retain idle bus levels for wiring
                // diagnosis; only the acquisition owner touches the bus.
                if (f==FDC_IO_ERROR) {
                    esp_err_t at_2a=i2c_master_probe(hardware.bus,0x2A,CONFIG_FDC_TRANSACTION_TIMEOUT_MS);
                    esp_err_t at_2b=i2c_master_probe(hardware.bus,0x2B,CONFIG_FDC_TRANSACTION_TIMEOUT_MS);
                    // Release the controller before testing the external bus.
                    // GPIO input mode with both pulls disabled makes these
                    // levels independent of an aborted I2C transaction.
                    close_bus();
                    const gpio_config_t released={
                        .pin_bit_mask=(UINT64_C(1)<<configuration.scl_gpio)|(UINT64_C(1)<<configuration.sda_gpio),
                        .mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_DISABLE,
                        .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
                    ESP_ERROR_CHECK(gpio_config(&released));
                    vTaskDelay(pdMS_TO_TICKS(3));
                    snprintf(reason,sizeof(reason),"i2c_error probe2a=%d probe2b=%d released_scl=%d released_sda=%d",
                        (int)at_2a,(int)at_2b,gpio_get_level((gpio_num_t)configuration.scl_gpio),
                        gpio_get_level((gpio_num_t)configuration.sda_gpio));
                    publish_state(STATE_FAULT,reason,&device,device.transport_error);
                } else publish_state(STATE_FAULT,fdc_result_name(f),&device,device.transport_error);
                vTaskDelay(pdMS_TO_TICKS(20));continue;
            }
            xSemaphoreTake(state_lock,portMAX_DELAY);
            fdc_ranges_clear(&shared.ranges);memset(&shared.frame,0,sizeof(shared.frame));
            xSemaphoreGive(state_lock);
            publish_state(STATE_RUNNING,"four-channel sequential scan",&device,0);
        }
        if (snapshot().state!=STATE_RUNNING) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
        fdc_frame_t frame;
        fdc_result_t result=fdc_poll(&device,&frame);
        if (result==FDC_OK) {
            xSemaphoreTake(state_lock,portMAX_DELAY);
            fdc_ranges_accept(&shared.ranges,&frame);shared.frame=frame;shared.sequence++;
            xSemaphoreGive(state_lock);
#if CONFIG_FDC_AUDIO_ENABLE
            audio_output_submit(&frame);
#endif
        } else if (result!=FDC_NO_DATA) {
            xSemaphoreTake(state_lock,portMAX_DELAY);fdc_ranges_io_error(&shared.ranges);xSemaphoreGive(state_lock);
            publish_state(STATE_FAULT,fdc_result_name(result),&device,device.transport_error);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void print_ranges(const snapshot_t *s)
{
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++) {
        const fdc_channel_range_t *r=&s->ranges.channel[ch];
        if (r->has_range) record_line("range,%"PRIu64",%u,%"PRIu64",%"PRIu64",1,%"PRIu32",%"PRIu32",%"PRIu32,
            s->ranges.epoch,ch,r->valid_count,r->error_count,r->minimum,r->maximum,fdc_range_span(r));
        else record_line("range,%"PRIu64",%u,%"PRIu64",%"PRIu64",0,,,",s->ranges.epoch,ch,r->valid_count,r->error_count);
    }
}
static void print_status(const snapshot_t *s)
{
    uint64_t age=s->frame.time_us ? (uint64_t)esp_timer_get_time()-s->frame.time_us:0;
    record_line("# state=%s reason=%s epoch=%"PRIu64" sequence=%"PRIu64" frame_age_us=%"PRIu64,
        state_name(s->state),s->reason,s->ranges.epoch,s->sequence,age);
    record_line("# complete_frames=%"PRIu64" incomplete_or_io_errors=%"PRIu64" last_reg=0x%02x expected=0x%04x actual=0x%04x transport=%d(%s) heap=%"PRIu32,
        s->ranges.frames,s->ranges.transport_errors,s->last_reg,s->expected,s->actual,s->error,
        esp_err_to_name(s->error),(uint32_t)esp_get_free_heap_size());
    record_line("# sda=%d scl=%d sd=%d addr_gpio=%d i2c_addr=0x%02x clock=%u fref_div=%u stream=%u output_dropped_lines=%"PRIu64,
        configuration.sda_gpio,configuration.scl_gpio,configuration.sd_gpio,configuration.addr_gpio,
        configuration.address,(unsigned)configuration.clock,configuration.fref_divider,s->stream,s->output_dropped_lines);
}
static void output_task(void *argument)
{
    (void)argument;
    uint64_t last_sequence=0,last_epoch=UINT64_MAX;
    state_t last_state=(state_t)-1;
    int64_t last_ranges=0;
    for (;;) {
        snapshot_t s=snapshot();
        if (s.state!=last_state) {
            record_line("event,%"PRIi64",%s,%s,reg_0x%02x,error_%d",esp_timer_get_time(),state_name(s.state),s.reason,s.last_reg,s.error);
            last_state=s.state;
        }
        if (s.ranges.epoch!=last_epoch) {
            record_line("event,%"PRIi64",epoch,%"PRIu64,esp_timer_get_time(),s.ranges.epoch);
            last_epoch=s.ranges.epoch;
        }
        if (s.stream && s.state==STATE_RUNNING && s.ranges.frames && s.frame.read_mask==0x0F && s.sequence!=last_sequence) {
            uint64_t skipped=s.sequence>last_sequence ? s.sequence-last_sequence-1:0;
            const fdc_frame_t *f=&s.frame;
            char line[320];
            int used=snprintf(line,sizeof(line),"sample,%"PRIu64",%"PRIu64",%"PRIu64",%u,0x%04x,0x%04x,%u,%u,%"PRIu64,
                s.ranges.epoch,s.sequence,f->time_us,f->valid_mask,f->status,f->status_errors,
                f->status_error_channels,f->ratio_error_mask,skipped);
            for (unsigned ch=0;ch<FDC_CHANNELS;ch++) used+=snprintf(line+(size_t)used,sizeof(line)-(size_t)used,",%"PRIu32",%u",f->raw[ch],f->flags[ch]);
            record_line("%s",line);last_sequence=s.sequence;
        }
        if (s.stream && s.state==STATE_RUNNING && esp_timer_get_time()-last_ranges>=1000000) {
            print_ranges(&s);last_ranges=esp_timer_get_time();
        }
#if CONFIG_FDC_AUDIO_ENABLE
        if (s.stream) audio_output_log(record_line,false);
#endif
        // Formatting and host backpressure never run in the acquisition owner.
        vTaskDelay(pdMS_TO_TICKS(CONFIG_FDC_OUTPUT_INTERVAL_MS));
    }
}

static int command_status(int argc,char **argv) {(void)argc;(void)argv;snapshot_t s=snapshot();print_status(&s);return 0;}
static int command_stats(int argc,char **argv) {(void)argc;(void)argv;snapshot_t s=snapshot();print_ranges(&s);return 0;}
#if CONFIG_FDC_AUDIO_ENABLE
static int command_audio(int argc,char **argv) {return audio_output_command(argc,argv,record_line);}
#endif
static int command_clear(int argc,char **argv)
{
    (void)argc;(void)argv;xTaskNotify(acquisition_handle,REQUEST_CLEAR,eSetBits);
    record_line("# clear queued; wait for the next epoch event/status before marking a measurement interval");return 0;
}
static int command_restart(int argc,char **argv)
{
    (void)argc;(void)argv;xTaskNotify(acquisition_handle,REQUEST_RESTART,eSetBits);
    record_line("# restart queued; configuration gate is checked again; successful init starts a new epoch");return 0;
}
static int command_stream(int argc,char **argv)
{
    if (argc!=2 || (strcmp(argv[1],"on") && strcmp(argv[1],"off"))) {record_line("# usage: stream on|off");return 1;}
    xSemaphoreTake(state_lock,portMAX_DELAY);shared.stream=!strcmp(argv[1],"on");xSemaphoreGive(state_lock);
    record_line("# stream setting changed; acquisition/range statistics continue independently");return 0;
}
static void register_command(const char *name,const char *help,esp_console_cmd_func_t function)
{
    const esp_console_cmd_t command={.command=name,.help=help,.func=function};
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
}

void app_main(void)
{
    configuration=(fdc_config_t){
#if CONFIG_FDC_WIRING_CONFIRMED
        .wiring_confirmed=true,
#endif
        .sda_gpio=CONFIG_FDC_SDA_GPIO,.scl_gpio=CONFIG_FDC_SCL_GPIO,
        .sd_gpio=CONFIG_FDC_SD_GPIO,.addr_gpio=CONFIG_FDC_ADDR_GPIO,.address=CONFIG_FDC_I2C_ADDRESS,
#if CONFIG_FDC_CLOCK_EXTERNAL
        .clock=FDC_CLOCK_EXT,.fref_divider=CONFIG_FDC_EXTERNAL_FREF_DIVIDER,
#elif CONFIG_FDC_CLOCK_INTERNAL
        .clock=FDC_CLOCK_INT,.fref_divider=CONFIG_FDC_INTERNAL_FREF_DIVIDER,
#else
        .clock=FDC_CLOCK_UNKNOWN,.fref_divider=0,
#endif
        .external_hz=CONFIG_FDC_EXTERNAL_CLOCK_HZ,.rcount=CONFIG_FDC_RCOUNT,.settlecount=CONFIG_FDC_SETTLECOUNT,
        .idrive={CONFIG_FDC_IDRIVE_CH0,CONFIG_FDC_IDRIVE_CH1,CONFIG_FDC_IDRIVE_CH2,CONFIG_FDC_IDRIVE_CH3},
        .data_timeout_ms=CONFIG_FDC_DATA_TIMEOUT_MS};
    // Driver diagnostics are returned in snapshots; avoid synchronous driver
    // error logs blocking the acquisition owner behind a disconnected console.
    esp_log_level_set("i2c.master",ESP_LOG_NONE);
    state_lock=xSemaphoreCreateMutex();ESP_ERROR_CHECK(state_lock ? ESP_OK:ESP_ERR_NO_MEM);
    shared.stream=true;shared.state=STATE_UNCONFIGURED;snprintf(shared.reason,sizeof(shared.reason),"waiting for configuration gate");
    puts("# FDC2214 raw debug: four sequential channels, no FIFO/scan-atomic guarantee");
    puts("# sample columns: type,epoch,sequence,time_us,valid_mask,status,status_errors,error_channels,ratio_errors,output_skipped,ch0,flags0,ch1,flags1,ch2,flags2,ch3,flags3");
    puts("# range columns: type,epoch,channel,valid_count,error_count,has_range,min,max,span");
    printf("# configured: SDA=%d SCL=%d SD=%d ADDR_GPIO=%d address=0x%02x clock_source=%u external_hz=%"PRIu32"\n",
        configuration.sda_gpio,configuration.scl_gpio,configuration.sd_gpio,configuration.addr_gpio,
        configuration.address,(unsigned)configuration.clock,configuration.external_hz);
    esp_console_repl_t *repl=NULL;
    esp_console_repl_config_t repl_config=ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt="fdc> ";repl_config.task_stack_size=4096;
    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_config,&repl));
    register_command("status","Show configuration gate, faults, freshness and heap",command_status);
    register_command("stats","Print all four channel ranges (blank means no valid sample)",command_stats);
    register_command("clear","Queue statistics reset; a new epoch marks completion",command_clear);
    register_command("restart","Recheck gate and reinitialize FDC; no automatic fault retry",command_restart);
    register_command("stream","stream on|off: CSV output only; acquisition remains active",command_stream);
#if CONFIG_FDC_AUDIO_ENABLE
    esp_err_t audio_error=audio_output_start(&configuration);
    if (audio_error!=ESP_OK) record_line("# audio init failed: %s; FDC raw acquisition remains available",esp_err_to_name(audio_error));
    register_command("audio","audio show|off|reference|sensor|level PERCENT|tone HZ",command_audio);
    register_command("vol","vol show|capture loud|capture mute|range LOUD MUTE|apply|channel N|curve linear|square|export",command_audio);
    puts("# I2S test starts silent; audio reference checks DAC, audio sensor enables calibrated volume");
    puts("# volume columns: type,time_us,channel,raw,u,gain_q16,level_percent,signal_gain,mode,quality,capture_result,capture_count,capture_aw,calibrated,driver_error");
#endif
    ESP_ERROR_CHECK(xTaskCreate(acquisition_task,"fdc_acquire",4096,NULL,5,&acquisition_handle)==pdPASS ? ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(output_task,"fdc_output",6144,NULL,1,NULL)==pdPASS ? ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
