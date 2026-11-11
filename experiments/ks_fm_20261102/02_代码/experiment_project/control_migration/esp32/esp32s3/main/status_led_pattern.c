#include "status_led_pattern.h"

static bool double_flash(uint32_t ms)
{
    unsigned phase = ms % 2000u;
    return phase < 100u || (phase >= 200u && phase < 300u);
}

status_led_levels_t status_led_pattern(const status_led_state_t *s, uint32_t now)
{
    uint32_t elapsed = now - s->started_ms;
    bool alive = s->engine_seen && s->voice_seen &&
                 now - s->engine_ms < 1000u && now - s->voice_ms < 1000u;
    bool stalled = s->boot_complete ?
                   (!alive && now - s->running_ms >= 1000u) : elapsed >= 5000u;
    bool red = false, blue;
    if (s->boot_complete && !s->boot_spi_ok) red = true;
    else if (stalled) red = elapsed % 250u < 125u;
    else if (s->engine_recovering) red = elapsed % 500u < 250u;
    else if (s->flags & (STATUS_BLE_FAULT | STATUS_WIFI_FAULT))
        red = double_flash(elapsed);

    if (!s->boot_complete) blue = double_flash(elapsed);
    else blue = alive && elapsed % 1000u < 100u;

    bool connected = ((s->flags & STATUS_BLE_READY) &&
                      (s->flags & STATUS_BLE_CONNECTED)) ||
                     ((s->flags & STATUS_WIFI_READY) &&
                      (s->flags & STATUS_WIFI_CONNECTED));
    bool ready = s->flags & (STATUS_BLE_READY | STATUS_WIFI_READY);
    bool green = connected || (ready && elapsed % 2000u < 100u);
    return (status_led_levels_t){.red = !red, .green = !green, .blue = !blue};
}
