#include "esp_timer.h"
#include "freertos/task.h"
#include "engine_link.h"
__attribute__((weak)) void engine_link_audio_routes_state(engine_audio_routes_state_t *s)
{*s=(engine_audio_routes_state_t){0};}
__attribute__((weak)) void engine_link_cpu_mode_state(engine_cpu_mode_state_t *s)
{*s=(engine_cpu_mode_state_t){0};}
__attribute__((weak)) bool engine_link_cpu_mode_request(uint32_t mode,uint32_t id)
{(void)mode;(void)id;return false;}
// Legacy engine fixtures do not connect a CPU endpoint. Real CPU/SPI tests
// link the production identity/authorization functions over these defaults.
__attribute__((weak)) bool ae350_control_identity(void){return false;}
__attribute__((weak)) void fpga_cpu_link_authorize(bool valid){(void)valid;}
int64_t fake_time;
uint32_t esp_random(void) { static uint32_t nonce=0x12345678;return ++nonce; }
int64_t esp_timer_get_time(void) { return fake_time++; }
void esp_rom_delay_us(unsigned us) { fake_time += us; }
int esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *t) {(void)a;*t=(void*)1;return 0;}
int esp_timer_start_periodic(esp_timer_handle_t t,uint64_t p) {(void)t;(void)p;return 0;}
BaseType_t xTaskCreate(void (*f)(void*),const char*n,unsigned s,void*a,unsigned p,TaskHandle_t*t) {(void)f;(void)n;(void)s;(void)a;(void)p;if(t)*t=(void*)1;return pdPASS;}
void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
unsigned ulTaskNotifyTake(BaseType_t c,TickType_t w) {(void)c;(void)w;return 1;}
void vTaskDelay(TickType_t t) {fake_time += t*10000;}
