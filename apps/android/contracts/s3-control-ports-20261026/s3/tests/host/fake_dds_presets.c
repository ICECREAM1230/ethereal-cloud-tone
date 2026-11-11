// Legacy control tests isolate file IO. New DDS owner/worker tests link the
// production service instead; these weak boundaries never override it.
#include "dds_presets.h"
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
WEAK void dds_presets_status(dds_job_status_t *s){*s=(dds_job_status_t){0};}
WEAK char *dds_presets_result(uint32_t i,size_t *l){(void)i;*l=0;return 0;}

WEAK bool dds_presets_result_acquire(uint32_t i,dds_result_view_t *v){(void)i;*v=(dds_result_view_t){0};return false;}
WEAK void dds_presets_result_release(dds_result_view_t *v){*v=(dds_result_view_t){0};}
WEAK bool dds_presets_request_owned(const char *p,char **t,size_t n,uint32_t *i,char *e)
{(void)p;(void)t;(void)n;(void)i;(void)e;return false;}
WEAK bool dds_presets_import_owned(const char *p,char **t,size_t n,uint32_t *i,char *e)
{(void)p;(void)t;(void)n;(void)i;(void)e;return false;}
WEAK bool dds_presets_source_valid(uint32_t i,int64_t now)
{(void)i;(void)now;return true;}
WEAK void dds_presets_owner_cancel(uint32_t i,uint32_t c,const char *e)
{(void)i;(void)c;(void)e;}
WEAK bool dds_presets_request_source(control_source_ref_t s,dds_operation_t op,const char *p,const char *t,size_t n,unsigned a,uint32_t *i,char *e)
{(void)s;return dds_presets_request(op,p,t,n,a,i,e);}
WEAK bool dds_presets_request_owned_source(control_source_ref_t s,const char *p,char **t,size_t n,uint32_t *i,char *e)
{(void)s;return dds_presets_request_owned(p,t,n,i,e);}
WEAK bool dds_presets_import_owned_source(control_source_ref_t s,const char *p,char **t,size_t n,uint32_t *i,char *e)
{(void)s;return dds_presets_import_owned(p,t,n,i,e);}
