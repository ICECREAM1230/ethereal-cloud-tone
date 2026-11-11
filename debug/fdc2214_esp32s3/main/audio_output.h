#pragma once
#include "esp_err.h"
#include "fdc2214.h"

typedef void (*audio_print_t)(const char *format,...);
esp_err_t audio_output_start(const fdc_config_t *fdc);
void audio_output_submit(const fdc_frame_t *frame);
void audio_output_fault(void);
int audio_output_command(int argc,char **argv,audio_print_t print);
void audio_output_log(audio_print_t print,bool full);
