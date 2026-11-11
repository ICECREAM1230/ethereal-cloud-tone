#include <stdint.h>
#include "control_service.h"
#if ENABLE_AE350_FDC
#include "fdc_application.h"
#endif

/* Headless control candidate only. The FPGA menu service still owns wall
 * time and the debug scratch register; no screen is rendered. */
static cs_service_t controls;
static inline uint32_t rd(unsigned off) {
    uint32_t v=*(volatile uint32_t *)(uintptr_t)(0xf8000000u+off);
    __asm__ volatile("fence iorw,iorw" ::: "memory");return v;
}
static inline void wr(unsigned off,uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(0xf8000000u+off)=v;
    __asm__ volatile("fence iorw,iorw" ::: "memory");
}
int main(void) {
    unsigned boot_marked=0,fault_marked=0;
    wr(0x1c,0xae350010u);
    if(rd(0)!=0x4d454e55u || rd(4)!=0x10000u) {
        wr(0x1c,0xae3500e1u);for(;;){}
    }
    wr(0x1c,0xae350011u);
    (void)cs_init(&controls,0,7,0,rd(8));
    (void)cs_enable_channel_routes(&controls);
#if ENABLE_AE350_FDC
    if(fdc_application_init(&controls,rd(8)))
        cs_set_fdc_config_handler(&controls,fdc_application_record,0);
#endif
    for(;;) {
        uint32_t now=rd(8);
        (void)cs_poll(&controls,now,&cs_default_budget);
#if ENABLE_AE350_FDC
        fdc_application_poll(now);
        cs_set_fdc_status(&controls,fdc_application_status_word());
        uint32_t fdc_words[3];fdc_application_telemetry(fdc_words);
        cs_set_fdc_telemetry(&controls,fdc_words);
#endif
        if(controls.stats.fault && !fault_marked) {
            wr(0x1c,0xae3500e1u);fault_marked=1;
        }
        /* Preserve the original UART-visible boot values. In this headless
         * variant 0x21 means the real control service entered RUNNING, not
         * that a text frame was rendered or acknowledged. */
        if(!boot_marked && !fault_marked && controls.stats.state==CS_RUNNING) {
            wr(0x1c,0xae350020u);
            wr(0x1c,0xae350021u);
            boot_marked=1;
        }
    }
}
