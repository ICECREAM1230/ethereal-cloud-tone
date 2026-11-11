/* AE350 application adapter example, deliberately independent of a BSP.
 * Add this file and theremin_control.c to your AE350 SDK application.
 * The caller owns I2C and converts the platform timer into64-bit microseconds.
 * No MCU register, FPGA project, interrupt, or transport is configured here.
 */
#include "theremin_control.h"

static theremin_t controller;

bool ae350_theremin_example_init(void)
{
    theremin_config_t config=theremin_default_config();
    /* Replace config.pitch/volume endpoints after better wiring/calibration. */
    return theremin_init(&controller,&config);
}

bool ae350_theremin_example_tick(uint64_t now_us,
                                const theremin_sample_t *new_ch0,
                                const theremin_sample_t *new_ch1,
                                theremin_output_t *controls)
{
    /* Call periodically even if one/both pointers are NULL. Only fresh channel
     * measurements advance their age. flags=(DATA_MSB>>12)&3; hard_error must
     * include I2C failures, conversion timeout and known out-of-range status.
     * AW alone does not set hard_error under the preview policy.
     */
    return theremin_update(&controller,now_us,new_ch0,new_ch1,controls);
    /* Application consumes controls->pitch_code (or signed pitch_delta), and
     * controls->gain_q16. Transfer them coherently; do not truncate65536 to16bits.
     * Smooth/interpolate control targets in the downstream audio-frame domain.
     */
}
