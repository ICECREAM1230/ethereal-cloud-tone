/* One compressed resource in flash. Identity clients use the S3 ROM inflater
 * with a 32 KiB dictionary, never a full-page allocation. IDF v6.1 miniz.h. */
#include <stdlib.h>
#include "miniz.h"
static esp_err_t page_identity(httpd_req_t *req)
{
    const unsigned char *gzip=(const unsigned char *)page_gzip_start;
    size_t length=(size_t)(page_gzip_end-page_gzip_start);
    // compress_web.py emits a fixed 10-byte gzip header with no optional fields.
    if(length<18 || gzip[0]!=31 || gzip[1]!=139 || gzip[2]!=8 || gzip[3]!=0)return ESP_FAIL;
    struct inflater { tinfl_decompressor decoder; unsigned char dictionary[32768]; };
    struct inflater *work=malloc(sizeof(*work));
    if(!work)return reply(req,"503 Service Unavailable","{\"error\":\"page_memory_unavailable\"}");
    tinfl_init(&work->decoder);
    size_t cursor=10,total=0,end=length-8;
    uint32_t expected=(uint32_t)gzip[end+4] | (uint32_t)gzip[end+5]<<8 |
        (uint32_t)gzip[end+6]<<16 | (uint32_t)gzip[end+7]<<24;
    esp_err_t result=ESP_FAIL;
    while(cursor<=end) {
        size_t input=end-cursor,output=sizeof(work->dictionary);
        tinfl_status status=tinfl_decompress(&work->decoder,gzip+cursor,&input,
            work->dictionary,work->dictionary,&output,0);
        cursor+=input;total+=output;
        if(status<TINFL_STATUS_DONE)break;
        if(output && httpd_resp_send_chunk(req,(const char *)work->dictionary,output)!=ESP_OK)break;
        if(status==TINFL_STATUS_DONE) {
            if(cursor==end && total==expected)result=httpd_resp_send_chunk(req,NULL,0);
            break;
        }
        if(status!=TINFL_STATUS_HAS_MORE_OUTPUT || (!input && !output))break;
    }
    free(work);return result;
}
