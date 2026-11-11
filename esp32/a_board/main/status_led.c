#include "sdkconfig.h"
#include "board_pins.h"
#include "status_led.h"
#if BOARD_STATUS_LEDS
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_control.h"

_Static_assert((((1ULL << STATUS_LED_RED_PIN) | (1ULL << STATUS_LED_GREEN_PIN) |
                 (1ULL << STATUS_LED_BLUE_PIN)) &
                ((1ULL << PANEL_MIDI_RX_PIN) | (1ULL << SLIDER_ADC_GPIO_PIN))) == 0,
               "LED outputs cannot share ADC/UART panel inputs");
_Static_assert(GPIO_IS_VALID_OUTPUT_GPIO(STATUS_LED_RED_PIN) &&
               GPIO_IS_VALID_OUTPUT_GPIO(STATUS_LED_GREEN_PIN) &&
               GPIO_IS_VALID_OUTPUT_GPIO(STATUS_LED_BLUE_PIN),
               "status LEDs require output-capable GPIOs on this target");
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static status_led_state_t s_state;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void status_led_set_flag(status_led_flag_t flag, bool enabled)
{
    portENTER_CRITICAL(&s_lock);
    if (enabled) s_state.flags |= flag;
    else s_state.flags &= ~flag;
    portEXIT_CRITICAL(&s_lock);
}
void status_led_engine_heartbeat(void)
{
    uint32_t now = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_state.engine_ms = now; s_state.engine_seen = true;
    portEXIT_CRITICAL(&s_lock);
}
void status_led_voice_heartbeat(void)
{
    uint32_t now = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_state.voice_ms = now; s_state.voice_seen = true;
    portEXIT_CRITICAL(&s_lock);
}
void status_led_boot_complete(bool spi_ok)
{
    uint32_t now = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_state.boot_complete = true; s_state.boot_spi_ok = spi_ok;
    s_state.running_ms = now;
    portEXIT_CRITICAL(&s_lock);
}
static void led_task(void *arg)
{
    (void)arg;
    while (1) {
        status_led_state_t state;
        portENTER_CRITICAL(&s_lock); state = s_state; portEXIT_CRITICAL(&s_lock);
        // RAM observation only: this task never calls SPI or edits sound state.
        web_control_state_t web;
        web_control_read(&web);
        state.engine_recovering = web.engine.recovering;
        status_led_levels_t levels = status_led_pattern(&state, now_ms());
        ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_RED_PIN, levels.red));
        ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_GREEN_PIN, levels.green));
        ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_BLUE_PIN, levels.blue));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void status_led_init(void)
{
    // Preload the OFF level before enabling outputs, avoiding a low glitch.
    ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_RED_PIN, 1));
    ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_GREEN_PIN, 1));
    ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_BLUE_PIN, 1));
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << STATUS_LED_RED_PIN) |
                        (1ULL << STATUS_LED_GREEN_PIN) |
                        (1ULL << STATUS_LED_BLUE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    s_state = (status_led_state_t){.started_ms = now_ms()};
    if (xTaskCreate(led_task, "status_led", 3072, NULL, 1, NULL) != pdPASS) {
        ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_RED_PIN, 0));
        ESP_LOGE("status_led", "task allocation failed; red LED held on");
        return;
    }
    ESP_LOGI("status_led", "active low: red=%d green=%d blue=%d",
             STATUS_LED_RED_PIN, STATUS_LED_GREEN_PIN, STATUS_LED_BLUE_PIN);
}
#endif
