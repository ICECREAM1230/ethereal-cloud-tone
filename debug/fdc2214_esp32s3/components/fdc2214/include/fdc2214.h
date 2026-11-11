#ifndef FDC2214_RAW_DEBUG_H
#define FDC2214_RAW_DEBUG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FDC_CHANNELS 4u
#define FDC_STATUS_ERRORS UINT16_C(0x0E00)
#define FDC_MUX_FOUR_CHANNELS UINT16_C(0xC20D)

typedef enum { FDC_CLOCK_UNKNOWN, FDC_CLOCK_EXT, FDC_CLOCK_INT } fdc_clock_t;
typedef struct {
    bool wiring_confirmed;
    int sda_gpio, scl_gpio, sd_gpio, addr_gpio;
    uint8_t address;
    fdc_clock_t clock;
    uint32_t external_hz;
    uint16_t fref_divider, rcount, settlecount;
    uint8_t idrive[FDC_CHANNELS];
    uint32_t data_timeout_ms;
} fdc_config_t;

typedef struct {
    void *context;
    int (*read)(void *context, uint8_t reg, uint16_t *value);
    int (*write)(void *context, uint8_t reg, uint16_t value);
    uint64_t (*now_us)(void *context);
} fdc_io_t;

typedef enum {
    FDC_OK=0, FDC_NO_DATA=1, FDC_UNCONFIGURED=-1, FDC_IO_ERROR=-2,
    FDC_ID_ERROR=-3, FDC_VERIFY_ERROR=-4, FDC_DATA_TIMEOUT=-5
} fdc_result_t;

typedef struct {
    uint64_t time_us;
    uint32_t raw[FDC_CHANNELS];
    uint8_t flags[FDC_CHANNELS]; // bit1 watchdog, bit0 amplitude warning
    uint8_t read_mask, valid_mask, ratio_error_mask, status_error_channels;
    uint16_t status, status_errors;
} fdc_frame_t;

typedef struct {
    fdc_io_t io;
    fdc_config_t config;
    bool configured;
    fdc_result_t result;
    uint8_t last_reg, pending_error_channels, last_error_channel;
    uint16_t expected, actual, pending_errors;
    int transport_error;
    uint64_t last_frame_us;
} fdc_device_t;

bool fdc_config_valid(const fdc_config_t *cfg, char *reason, size_t size);
uint16_t fdc_be16(const uint8_t bytes[2]);
uint32_t fdc_raw28(uint16_t msb, uint16_t lsb);
fdc_result_t fdc_init(fdc_device_t *dev, const fdc_config_t *cfg, const fdc_io_t *io);
// Each DATA MSB/LSB register has its own I2C register transaction. Returns
// FDC_OK only after all eight reads complete; failure clears the caller frame.
fdc_result_t fdc_poll(fdc_device_t *dev, fdc_frame_t *frame);
const char *fdc_result_name(fdc_result_t result);
#endif
