/* Host SDK boundary for the S3 ROM streaming inflater. Actual image links ROM. */
#pragma once
#include <zlib.h>
#include <string.h>
typedef enum { TINFL_STATUS_FAILED=-1,TINFL_STATUS_DONE=0,TINFL_STATUS_HAS_MORE_OUTPUT=2 } tinfl_status;
typedef struct { z_stream z; int initialized; } tinfl_decompressor;
#define tinfl_init(p) memset((p),0,sizeof(*(p)))
static inline tinfl_status tinfl_decompress(tinfl_decompressor *p,const unsigned char *input,
    size_t *input_size,unsigned char *base,unsigned char *output,size_t *output_size,unsigned flags)
{
    (void)base;(void)flags;
    if(!p->initialized){if(inflateInit2(&p->z,-15)!=Z_OK)return TINFL_STATUS_FAILED;p->initialized=1;}
    p->z.next_in=(Bytef *)input;p->z.avail_in=(uInt)*input_size;
    p->z.next_out=output;p->z.avail_out=(uInt)*output_size;
    int status=inflate(&p->z,Z_NO_FLUSH);
    *input_size-=p->z.avail_in;*output_size-=p->z.avail_out;
    if(status==Z_STREAM_END){inflateEnd(&p->z);p->initialized=0;return TINFL_STATUS_DONE;}
    if(status!=Z_OK || !*output_size){inflateEnd(&p->z);p->initialized=0;return TINFL_STATUS_FAILED;}
    return TINFL_STATUS_HAS_MORE_OUTPUT;
}
