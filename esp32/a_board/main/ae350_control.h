#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_FPGA_AE350_CONTROL
#define CONFIG_FPGA_AE350_CONTROL 0
#endif
#define AE350_CPU_MAGIC_REG UINT16_C(0x0718)
#define AE350_CPU_ABI_REG UINT16_C(0x0719)
#define AE350_CPU_MAGIC UINT32_C(0x43505531)
#define AE350_CPU_ABI UINT32_C(2)
#define AE350_CPU_CAP UINT32_C(4)
#define AE350_TEMPLATE_CREDIT_REG UINT16_C(0x071c)
#define AE350_TEMPLATE_CAPACITY 64u
#define AE350_TEMPLATE_CREDIT_RESERVE 2u
/* Only the existing SPI owner calls identity/authorize/service. */
bool ae350_control_identity(void);
bool ae350_control_write_allowed(uint16_t address,size_t words);
void fpga_cpu_link_authorize(bool verified);
uint32_t fpga_cpu_rejected_writes(void);
