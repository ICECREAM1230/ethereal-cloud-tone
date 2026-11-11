#define main previous_kst_link_entry
#include "test_ks_timbre_link.c"
#undef main
static control_source_ref_t bind(unsigned model)
{
    control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,now,&source));
    midi_template_link_bind_timbre_source(model,source,now+CONTROL_SOURCE_LEASE_US);return source;
}
int main(void)
{
    control_source_init();fixture();control_source_ref_t source=bind(41);
    control_source_revoke(source);publish(1,41,3000);slices(12);
    assert(!commits && !ks_writes && read_state().ks_canceled_model_generation==41);
    fixture();source=bind(42);publish(1,42,3000);
    for(unsigned i=0;i<20 && !ks_writes;i++)step();
    assert(ks_writes && !commits);unsigned before=ks_writes;
    control_source_revoke(source);slices(12);
    assert(!commits && ks_writes==before && read_state().ks_canceled_model_generation==42);
    fixture();source=bind(43);auto_ack=false;publish(1,43,3000);slices(12);assert(commits==1);
    control_source_revoke(source);now+=100001;slices(4);
    assert(commits==1 && read_state().fault==MIDI_TEMPLATE_LINK_TIMEOUT);
    before=ks_writes;publish(2,44,3000);slices(4);assert(commits==1 && ks_writes==before);
    fixture();source=bind(45);publish(1,45,3000);slices(12);
    assert(read_state().ready && read_state().ks_applied_model_generation==45 && commits==1);
    control_source_revoke(source);publish(1,45,4000);slices(12);
    assert(commits==2 && read_state().ks_applied_model_generation==45 && read_state().ready);
    puts("PASS source KS native link: bind-before-publish, mid-upload revoke blocks COMMIT, unknown COMMIT retained, adopted shared refresh survives old source");
    return 0;
}
