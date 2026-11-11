/* Old control tests isolate the new additive transport; focused tests use real link. */
#include "additive_link.h"
#include <string.h>
static additive_link_state_t fake_additive;
static uint32_t fake_additive_request_id;
static unsigned fake_additive_cancels;
void additive_link_init(void) { memset(&fake_additive,0,sizeof(fake_additive)); }
void additive_link_invalidate(void) { fake_additive.valid=false; }
static bool fake_patch_known(void)
{
    // Older fixtures construct an acknowledged commit directly. Local
    // adoption clears these identities, so it never invents patch knowledge.
    return fake_additive.patch_known ||
        (fake_additive.valid && fake_additive.revision && fake_additive.commit_id);
}
void additive_link_state(additive_link_state_t *out)
{ *out=fake_additive;out->patch_known=fake_patch_known(); }
void additive_link_status(additive_link_status_t *out)
{
    if(!out)return;
    *out=(additive_link_status_t){
        .available=fake_additive.available,.valid=fake_additive.valid,
        .selected=fake_additive.selected,.ready=fake_additive.ready,.busy=fake_additive.busy,
        .patch_known=fake_patch_known(),
        .fault=fake_additive.fault,.instance_id=fake_additive.instance_id,
        .revision=fake_additive.revision,.commit_id=fake_additive.commit_id,
        .completed_request_id=fake_additive.completed_request_id,
    };
}
void additive_link_service(int64_t deadline,bool stopped) { (void)deadline;(void)stopped; }
bool additive_link_request(const additive_patch_t *patch,bool selected,uint32_t id)
{
    if(fake_additive.busy || !fake_additive.available)return false;
    fake_additive.busy=true;fake_additive.selected=selected;
    fake_additive.confirmed_patch=*patch;fake_additive_request_id=id;return true;
}
void additive_link_cancel(void) { fake_additive_cancels++;fake_additive.busy=true;fake_additive.valid=false;fake_additive.ready=false;fake_additive.patch_known=false; }
void additive_link_local_fence(bool fault)
{
    fake_additive.instance_id++;fake_additive.valid=fake_additive.ready=fake_additive.patch_known=false;
    fake_additive.busy=true;fake_additive.fault=fault?ADDITIVE_LINK_CANCELLED:ADDITIVE_LINK_NONE;
    fake_additive.revision=fake_additive.commit_id=fake_additive.completed_request_id=0;
    memset(&fake_additive.confirmed_patch,0,sizeof(fake_additive.confirmed_patch));
}
void additive_link_adopt_local(bool available,bool selected)
{
    fake_additive.available=fake_additive.valid=fake_additive.ready=available;
    fake_additive.selected=available && selected;fake_additive.busy=false;fake_additive.patch_known=false;
    fake_additive.fault=ADDITIVE_LINK_NONE;
    fake_additive.revision=fake_additive.commit_id=fake_additive.completed_request_id=0;
    memset(&fake_additive.confirmed_patch,0,sizeof(fake_additive.confirmed_patch));
}
void additive_link_remote_resume(void)
{
    fake_additive.valid=fake_additive.ready=fake_additive.patch_known=false;fake_additive.busy=true;
}
