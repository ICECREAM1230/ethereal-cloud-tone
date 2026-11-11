#include "platform.h"
#include "b_pinmap.h"
#include <string.h>
#include "driver/gpio.h"
#include "sdkconfig.h"

/* OUT1..OUT12, user-confirmed header map revision 4 (2026-10-30).
 * Legacy schematic IO labels do not match ten of these physical GPIOs. */
const int b_laser_pins[HARP_LASERS] = {4,5,46,6,18,7,9,15,10,16,11,17};
static b_encoder_pins_t current = {{-1,-1,-1},{-1,-1,-1},{-1,-1,-1},{-1,-1,-1}};

void b_board_defaults(b_encoder_pins_t pins)
{
    const b_encoder_pins_t initial = {
        {CONFIG_B_ENC1_A,CONFIG_B_ENC1_B,CONFIG_B_ENC1_SW},
        {CONFIG_B_ENC2_A,CONFIG_B_ENC2_B,CONFIG_B_ENC2_SW},
        {CONFIG_B_ENC3_A,CONFIG_B_ENC3_B,CONFIG_B_ENC3_SW},
        {CONFIG_B_ENC4_A,CONFIG_B_ENC4_B,CONFIG_B_ENC4_SW}
    };
    memcpy(pins, initial, sizeof(initial));
}

esp_err_t b_board_validate(const b_encoder_pins_t pins)
{
    return b_encoder_pins_valid(pins) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static uint64_t encoder_mask(const b_encoder_pins_t pins)
{
    uint64_t mask=0;
    for(unsigned i=0;i<HARP_ENCODERS;i++)
        for(unsigned p=0;p<3;p++) if(pins[i][p]>=0) mask|=1ULL<<pins[i][p];
    return mask;
}

static esp_err_t configure_inputs(uint64_t mask)
{
    if (!mask) return ESP_OK;
    const gpio_config_t config={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    return gpio_config(&config);
}

esp_err_t b_board_apply(const b_encoder_pins_t pins)
{
    esp_err_t rc=b_board_validate(pins);
    if (rc!=ESP_OK) return rc;
    uint64_t next=encoder_mask(pins), removed=encoder_mask(current)&~next;
    rc=configure_inputs(next);
    if (rc!=ESP_OK) return rc;
    for (unsigned p=0;p<49;p++) if (removed&(1ULL<<p)) {
        rc=gpio_reset_pin((gpio_num_t)p);
        if(rc!=ESP_OK) return rc;
    }
    memcpy(current,pins,sizeof(current));
    return ESP_OK;
}

esp_err_t b_board_init(const b_encoder_pins_t pins)
{
    uint64_t mask=0;
    esp_err_t rc=b_board_validate(pins);
    if(rc!=ESP_OK) return rc;
    for(unsigned i=0;i<HARP_LASERS;i++) mask|=1ULL<<b_laser_pins[i];
    rc=configure_inputs(mask);
    return rc==ESP_OK ? b_board_apply(pins) : rc;
}

uint8_t b_board_read(uint16_t *levels, uint8_t ab[4], uint8_t *pressed)
{
    *levels=0;*pressed=0;
    for(unsigned i=0;i<HARP_LASERS;i++)
        if(gpio_get_level((gpio_num_t)b_laser_pins[i])) *levels|=(uint16_t)(1u<<i);
    uint8_t enabled=0;
    for(unsigned i=0;i<HARP_ENCODERS;i++) {
        ab[i]=3;
        if(current[i][0]<0) continue;
        enabled|=(uint8_t)(1u<<i);
        ab[i]=(gpio_get_level(current[i][0])?2:0)|(gpio_get_level(current[i][1])?1:0);
        if(!gpio_get_level(current[i][2])) *pressed|=(uint8_t)(1u<<i);
    }
    return enabled;
}
