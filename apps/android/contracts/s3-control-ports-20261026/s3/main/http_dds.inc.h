// Copyright (c) 2026 ICECREAM1230
// DDS preset management API; the performance API and page are unchanged.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#include "dds_presets.h"
#include <stdlib.h>
#define DDS_HTTP_TRY(call) do { if((call)!=ESP_OK)return ESP_FAIL; } while(0)

static bool dds_decode(const char *input,char *out,size_t size)
{
    size_t n=0;
    while(*input) {
        unsigned char c=(unsigned char)*input++;
        if(c=='%') {
            if(!input[0] || !input[1])return false;
            unsigned value=0;
            for(unsigned i=0;i<2;i++) {
                unsigned char h=(unsigned char)input[i];unsigned d;
                if(h>='0' && h<='9')d=h-'0';else if(h>='a' && h<='f')d=h-'a'+10;else if(h>='A' && h<='F')d=h-'A'+10;else return false;
                value=value*16+d;
            }
            c=(unsigned char)value;input+=2;
        } else if(c=='+')c=' ';
        if(!c || n+1>=size)return false;
        out[n++]=(char)c;
    }
    out[n]=0;return true;
}
static bool dds_query(api_req_t *req,char *operation,char *path,uint32_t *number,bool command)
{
    char query[DDS_PATH_MAX*3+96];size_t n=api_query_len(req);
    operation[0]=path[0]=0;*number=0;
    if(!n)return !command;
    if(n>=sizeof(query) || api_query_copy(req,query,sizeof(query))!=ESP_OK)return false;
    unsigned seen=0;
    for(char *part=query;part && *part;) {
        char *next=strchr(part,'&');if(next)*next++=0;
        char *value=strchr(part,'=');if(!value)return false;*value++=0;
        unsigned bit;
        if(command && !strcmp(part,"op")){bit=1;if(!dds_decode(value,operation,16))return false;}
        else if(command && !strcmp(part,"path")){bit=2;if(!dds_decode(value,path,DDS_PATH_MAX))return false;}
        else if(!strcmp(part,command?"offset":"id")){bit=4;if(!query_u32(value,number))return false;}
        else return false;
        if(seen&bit || (next && !*next))return false;
        seen|=bit;part=next;
    }
    return !command || (seen&1);
}
static esp_err_t dds_accept(api_req_t *req,dds_operation_t op,const char *path,const char *text,size_t len,uint32_t offset,char **owned)
{
    uint32_t id;char error[DDS_ERROR_MAX];
    bool writes=op!=DDS_LIST && op!=DDS_READ;
    control_source_ref_t source={0};
    if(writes && !source_claim(req,&source))
        return reply(req,"409 Conflict","{\"dds_api\":1,\"error\":\"control source busy\"}");
    bool accepted=writes?(owned?(op==DDS_IMPORT?
        dds_presets_import_owned_source(source,path,owned,len,&id,error):
        dds_presets_request_owned_source(source,path,owned,len,&id,error)):
        dds_presets_request_source(source,op,path,text,len,offset,&id,error)):
        dds_presets_request(op,path,text,len,offset,&id,error);
    if(!accepted) {
        DDS_HTTP_TRY(json_begin(req));
        DDS_HTTP_TRY(api_set_status(req,strstr(error,"busy")?"409 Conflict":"400 Bad Request"));
        DDS_HTTP_TRY(json_append(req,"{\"dds_api\":1,\"error\":",sizeof("{\"dds_api\":1,\"error\":")-1));
        DDS_HTTP_TRY(json_string(req,error));
        DDS_HTTP_TRY(json_append(req,"}",1));
        return json_end(req);
    }
    char json[128];snprintf(json,sizeof(json),"{\"dds_api\":1,\"job_id\":%" PRIu32 ",\"phase\":\"wait_owner\"}",id);
    return reply(req,"202 Accepted",json);
}
static esp_err_t core_dds_list_http(api_req_t *req)
{
    if(!same_origin_command(req))return reply(req,"403 Forbidden","{\"error\":\"header required\"}");
    if(req->content_len)return reply(req,"400 Bad Request","{\"error\":\"empty body required\"}");
    // Pagination uses POST ?op=list&offset=next_offset; GET is the first page.
    return dds_accept(req,DDS_LIST,"",NULL,0,0,NULL);
}
static esp_err_t core_dds_command_http(api_req_t *req)
{
    if(!same_origin_command(req))return reply(req,"403 Forbidden","{\"error\":\"header required\"}");
    char operation[16],path[DDS_PATH_MAX];uint32_t offset;
    if(!dds_query(req,operation,path,&offset,true))return reply(req,"400 Bad Request","{\"error\":\"invalid DDS query\"}");
    dds_operation_t op;
    if(!strcmp(operation,"list"))op=DDS_LIST;else if(!strcmp(operation,"load"))op=DDS_LOAD;
    else if(!strcmp(operation,"save"))op=DDS_SAVE;else if(!strcmp(operation,"read"))op=DDS_READ;
    else if(!strcmp(operation,"write"))op=DDS_WRITE;else if(!strcmp(operation,"default"))op=DDS_DEFAULT;
    else if(!strcmp(operation,"import"))op=DDS_IMPORT;
    else return reply(req,"400 Bad Request","{\"error\":\"unknown DDS operation\"}");
    if((op!=DDS_LIST && offset) || req->content_len>DDS_TEXT_MAX || ((op==DDS_WRITE || op==DDS_IMPORT)?!req->content_len:req->content_len!=0))
        return reply(req,"400 Bad Request","{\"error\":\"invalid DDS body/offset\"}");
    char *body=NULL;size_t len=req->content_len;
    if(len) {
        if(req->input->owned_body) {
            body=*req->input->owned_body;*req->input->owned_body=NULL;
        } else {
            body=malloc(len+1);if(!body)return reply(req,"503 Service Unavailable","{\"error\":\"out of memory\"}");
            size_t got=0;
            while(got<len){int n=api_recv(req,body+got,len-got);if(n<=0){free(body);return reply(req,"400 Bad Request","{\"error\":\"incomplete DDS body\"}");}got+=(size_t)n;}
            body[len]=0;
        }
    }
    esp_err_t result=dds_accept(req,op,path,body,len,offset,(op==DDS_WRITE || op==DDS_IMPORT)?&body:NULL);free(body);return result;
}
static const char *dds_phase_name(dds_phase_t phase)
{
    static const char *const names[]={"idle","wait_owner","working","ready","closing","done","failed"};
    return phase<=DDS_FAILED?names[phase]:"unknown";
}
static esp_err_t core_dds_status_http(api_req_t *req)
{
    char op[16],unused[DDS_PATH_MAX];uint32_t id;
    if(req->content_len || !dds_query(req,op,unused,&id,false))return reply(req,"400 Bad Request","{\"error\":\"invalid DDS query\"}");
    dds_job_status_t status;api_dds_status(req,&status);
    if(id && status.id!=id)return reply(req,"404 Not Found","{\"error\":\"job retired or unknown\"}");
    DDS_HTTP_TRY(json_begin(req));
    DDS_HTTP_TRY(json_format(req,"{\"dds_api\":1,\"job_id\":%" PRIu32 ",\"phase\":",status.id));
    DDS_HTTP_TRY(json_string(req,dds_phase_name(status.phase)));
    DDS_HTTP_TRY(json_format(req,",\"operation\":%u,\"mounted\":%s,\"context_epoch\":%" PRIu32 ",\"warnings\":%u,\"next_offset\":%u,\"path\":",
        (unsigned)status.operation,status.mounted?"true":"false",status.context,status.warnings,status.next_offset));
    DDS_HTTP_TRY(json_string(req,status.path));
    DDS_HTTP_TRY(json_append(req,",\"error\":",9));
    DDS_HTTP_TRY(json_string(req,status.error));
    DDS_HTTP_TRY(json_append(req,",\"completion_scope\":\"owner_or_file_only\"}",sizeof(",\"completion_scope\":\"owner_or_file_only\"}")-1));
    return json_end(req);
}
static esp_err_t core_dds_result_http(api_req_t *req)
{
    char op[16],unused[DDS_PATH_MAX];uint32_t id;
    if(req->content_len || !dds_query(req,op,unused,&id,false) || !id)return reply(req,"400 Bad Request","{\"error\":\"DDS job id required\"}");
    dds_job_status_t status;api_dds_status(req,&status);
    if(status.id!=id)return reply(req,"404 Not Found","{\"error\":\"job retired or unknown\"}");
    if(status.phase!=DDS_DONE || (status.operation!=DDS_READ && status.operation!=DDS_LIST))return reply(req,"409 Conflict","{\"error\":\"no completed text result\"}");
    dds_result_view_t view;
    if(!api_dds_acquire(req,id,&view))return reply(req,"503 Service Unavailable","{\"error\":\"result unavailable\"}");
    api_set_status(req,"200 OK");api_set_type(req,"text/plain; charset=utf-8");
    api_set_header(req,"Cache-Control","no-store");api_set_header(req,"X-Content-Type-Options","nosniff");
    esp_err_t result=api_send(req,view.text,(int)view.len);
    return result;
}

#undef DDS_HTTP_TRY
