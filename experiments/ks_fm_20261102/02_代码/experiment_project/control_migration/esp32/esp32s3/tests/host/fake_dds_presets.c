// Legacy control tests isolate file IO. New DDS owner/worker tests link the
// production service instead; these weak boundaries never override it.
#include "dds_presets.h"
#include <stdlib.h>
#define WEAK __attribute__((weak))
WEAK bool dds_presets_boot_patch(patch_t *p){(void)p;return false;}
WEAK void dds_presets_owner_context(uint32_t c){(void)c;}
WEAK uint32_t dds_presets_waiting(void){return 0;}
WEAK void dds_presets_owner_begin(uint32_t i,const patch_t *p,uint32_t c,bool d){(void)i;(void)p;(void)c;(void)d;}
WEAK bool dds_presets_ready(uint32_t *i,patch_t *p,uint32_t *c){(void)i;(void)p;(void)c;return false;}
WEAK void dds_presets_owner_closing(uint32_t i){(void)i;}
WEAK void dds_presets_owner_complete(uint32_t i,uint32_t c,const char *e){(void)i;(void)c;(void)e;}
WEAK bool dds_presets_console_byte(uint8_t b){(void)b;return false;}
WEAK bool dds_presets_request(dds_operation_t op,const char *p,const char *t,size_t l,unsigned a,uint32_t *i,char *e)
{(void)op;(void)p;(void)t;(void)l;(void)a;(void)i;if(e)e[0]=0;return false;}
WEAK bool dds_presets_request_owned(dds_operation_t op,const char *p,char **t,size_t l,unsigned a,uint32_t *i,char *e)
{bool ok=dds_presets_request(op,p,t?*t:NULL,l,a,i,e);if(ok && t){free(*t);*t=NULL;}return ok;}
WEAK void dds_presets_status(dds_job_status_t *s){*s=(dds_job_status_t){0};}
WEAK char *dds_presets_result(uint32_t i,size_t *l){(void)i;*l=0;return 0;}
