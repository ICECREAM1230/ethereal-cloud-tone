#include "freertos/FreeRTOS.h"
static void concurrent_owner(void);
#undef portEXIT_CRITICAL
#define portEXIT_CRITICAL(p) ((void)(p),concurrent_owner())
#define main fm_lifecycle_regression
#include "test_fm_engine.c"
#undef main
static bool cancel_first_select;
static uint32_t previous_nonce;
static void concurrent_owner(void)
{
    // Simulate the other CPU after SELECT has captured a fresh nonce but
    // before the SPI owner installs that command as inflight.
    if(cancel_first_select && s_fm_nonce!=previous_nonce && !s_fm_inflight.active) {
        cancel_first_select=false;engine_link_fm_panic();
    }
}
int main(void)
{
    fixture();tick(2);previous_nonce=s_fm_nonce;
    assert(engine_link_fm_mode(1));cancel_first_select=true;tick(10);
    assert(!commits && !status().selected && !status().applied_backend);
    fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);
    complete=false;assert(engine_link_fm_preset(0)==0);tick(3);
    assert(s_fm_inflight.active && last.opcode_id==7);
    engine_link_fm_panic();assert(engine_link_fm_preset(1)==0);
    done();complete=true;tick(25);
    assert(panics==1 && last.opcode_id==7 && last.value_u32==1 && !s_fm_preset_pending);
    fixture();tick(2);assert(engine_link_fm_mode(1));complete=false;tick(2);
    done();engine_link_fm_panic();tick(20);
    assert(status().applied_backend==1 && status().requested_backend==1 && !status().switching);
    // Priority Panic exposes one result identity. A new accepted sequence can
    // equal the historical completed marker while the backend is still BUSY.
    fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);
    engine_link_fm_panic();tick(3);assert(panics==1 && !s_fm_panic.sequence_id);
    complete_panic=false;engine_link_fm_panic();tick(1);
    unsigned before=commits;assert(panics==2 && s_fm_panic.sequence_id==1);
    // The SPI owner can run between all_sound_off and the owner's mode call.
    assert(engine_link_fm_mode(0));tick(3);
    assert(panics==2 && s_fm_panic.sequence_id==1 && commits==before);
    assert(status().switching && !status().needs_select);
    panic_done();complete_panic=true;tick(5);
    assert(panics==2 && commits==before+1 && last.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND && last.value_u32==0);
    assert(!status().selected && !status().switching && !status().recovering);
    puts("FM Panic fence: historical same-sequence completion cannot cross BUSY; mode reuses the in-progress Panic PASS");
    puts("FM races: Panic between first SELECT capture/installation cancels it; older preset completion preserves newer ProgramChange intent PASS");
}
