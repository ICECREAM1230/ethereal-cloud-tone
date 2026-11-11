// Copyright (c) 2026 ICECREAM1230
// Inspired by Ethereal Cloud Tone safe_file, with CRC verification and retained old copy.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#define _POSIX_C_SOURCE 200809L
#include "dds_files.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail(char *error,const char *reason)
{snprintf(error,DDS_ERROR_MAX,"%s",reason);return false;}
static bool writable(const char *path)
{return !strcasecmp(path,"system.toml") || !strcasecmp(path,"banks.toml") || dds_path_valid(path,true);}
static void full_path(char *out,size_t size,const char *root,const char *relative,const char *suffix)
{snprintf(out,size,"%s/%s%s",root,relative,suffix);}
static uint32_t crc32(const char *text,size_t len)
{
    uint32_t crc=~0u;
    for(size_t i=0;i<len;i++){crc^=(uint8_t)text[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);}
    return ~crc;
}
static bool read_file(const char *file,char **text,size_t *len,char *error)
{
    FILE *in=fopen(file,"rb");if(!in)return fail(error,"cannot open document");
    bool ok=fseek(in,0,SEEK_END)==0;long n=ok?ftell(in):-1;
    if(n<1 || n>DDS_TEXT_MAX+80){fclose(in);return fail(error,"document size invalid");}
    char *data=malloc((size_t)n+1);
    if(!data){fclose(in);return fail(error,"out of memory");}
    ok=fseek(in,0,SEEK_SET)==0 && fread(data,1,(size_t)n,in)==(size_t)n;
    ok=fclose(in)==0 && ok;
    if(!ok){free(data);return fail(error,"cannot read document");}
    data[n]=0;*text=data;*len=(size_t)n;return true;
}
static bool marked(const char *text,size_t len,size_t *body,bool required)
{
    // The checksum authenticates completion, including truncation at a valid
    // TOML line boundary. It is not a security signature.
    size_t at=len;
    if(at && text[at-1]=='\n')at--;
    while(at && text[at-1]!='\n')at--;
    if(len-at<6 || strncmp(text+at,"# end ",6)){*body=len;return !required;}
    unsigned bytes,crc;int consumed=0;
    if(sscanf(text+at,"# end bytes=%u crc32=%x\n%n",&bytes,&crc,&consumed)!=2 ||
       consumed<=0 || at+(size_t)consumed!=len || bytes!=at || crc!=crc32(text,at))return false;
    *body=at;return true;
}
bool dds_files_read(const char *root,const char *path,char **text,size_t *len,char *error)
{
    if(!writable(path) && !dds_path_valid(path,false))return fail(error,"invalid document path");
    char file[DDS_PATH_MAX+80];full_path(file,sizeof(file),root,path,"");
    char *data;size_t n,body;
    if(!read_file(file,&data,&n,error))return false;
    if(!marked(data,n,&body,false) || body>DDS_TEXT_MAX){free(data);return fail(error,"document completion checksum failed");}
    data[body]=0;*text=data;*len=body;return true;
}
static bool valid_file(const char *file,const char *path,bool required)
{
    char *text,error[DDS_ERROR_MAX];size_t len,body;
    if(!read_file(file,&text,&len,error))return false;
    bool ok=marked(text,len,&body,required) && dds_document_validate(path,text,body,error);
    free(text);return ok;
}
static bool exists(const char *file)
{struct stat st;return stat(file,&st)==0;}
static bool remove_if_present(const char *file)
{return unlink(file)==0 || errno==ENOENT;}
static bool directory(const char *file)
{
    if(mkdir(file,0775)==0)return true;
    struct stat st;return errno==EEXIST && stat(file,&st)==0 && S_ISDIR(st.st_mode);
}
bool dds_files_layout(const char *root,char *error)
{
    char path[DDS_PATH_MAX+80];
    const char *dirs[]={"Patches","Patches/User"};
    for(unsigned i=0;i<2;i++){full_path(path,sizeof(path),root,dirs[i],"");if(!directory(path))return fail(error,"cannot create User directories");}
    return true;
}
static bool promote(const char *root,const char *path,const char *temporary,char *error)
{
    char final[DDS_PATH_MAX+80],tmp[DDS_PATH_MAX+80],backup[DDS_PATH_MAX+80];
    full_path(final,sizeof(final),root,path,"");full_path(tmp,sizeof(tmp),root,path,".new");full_path(backup,sizeof(backup),root,path,".bak");
    if(temporary)snprintf(tmp,sizeof(tmp),"%s",temporary);
    if(exists(final)) {
        // Do not replace a valid backup with a damaged target.
        if(valid_file(final,path,false)) {
            if(!remove_if_present(backup) || rename(final,backup)!=0)return fail(error,"cannot retain old document");
        } else if(!remove_if_present(final))return fail(error,"cannot remove damaged document");
    }
    if(rename(tmp,final)!=0)return fail(error,"save promotion failed; recoverable .new/.bak retained");
    return true;
}
bool dds_files_save(const char *root,const char *path,const char *text,size_t len,char *error)
{
    if(!writable(path))return fail(error,"Factory is read-only or path invalid");
    if(!dds_document_validate(path,text,len,error))return false;
    // Text imports may omit their final LF. The completion marker must still
    // start on its own line, and its CRC covers that normalization too.
    if(text[len-1]!='\n') {
        if(len>=DDS_TEXT_MAX)return fail(error,"no space for final newline");
        char *normalized=malloc(len+1);if(!normalized)return fail(error,"out of memory");
        memcpy(normalized,text,len);normalized[len]='\n';
        bool ok=dds_files_save(root,path,normalized,len+1,error);free(normalized);return ok;
    }
    if(!dds_files_layout(root,error))return false;
    char file[DDS_PATH_MAX+80];
    if(dds_path_valid(path,true)) {
        char category[DDS_PATH_MAX];strcpy(category,path);*strrchr(category,'/')=0;
        full_path(file,sizeof(file),root,category,"");if(!directory(file))return fail(error,"cannot create User category");
    }
    full_path(file,sizeof(file),root,path,".new");FILE *out=fopen(file,"wb");
    if(!out)return fail(error,"cannot create .new document");
    bool ok=fwrite(text,1,len,out)==len && fprintf(out,"# end bytes=%u crc32=%08x\n",(unsigned)len,(unsigned)crc32(text,len))>0 && fflush(out)==0 && fsync(fileno(out))==0;
    ok=fclose(out)==0 && ok;
    if(!ok){remove_if_present(file);return fail(error,"cannot persist .new document (storage full?)");}
    if(!valid_file(file,path,true))return fail(error,"new document readback failed");
    return promote(root,path,NULL,error);
}
bool dds_files_import(const char *root,const char *path,const char *text,size_t len,char *error)
{
    if(!dds_path_valid(path,true))return fail(error,"import requires a User preset path");
    // All DDS file mutation is serialized by the single worker. Checking all
    // three names here closes the cross-client list/check/create window and
    // preserves interrupted operations for the existing recovery path.
    const char *suffixes[]={"",".new",".bak"};
    char file[DDS_PATH_MAX+80];struct stat st;
    for(unsigned i=0;i<3;i++) {
        full_path(file,sizeof(file),root,path,suffixes[i]);
        if(stat(file,&st)==0)return fail(error,"target exists; explicit replacement required");
        if(errno!=ENOENT)return fail(error,"cannot inspect import target");
    }
    return dds_files_save(root,path,text,len,error);
}
// Iterative traversal avoids four copies of the path buffers on the stack.
// The initial IDF task has a small stack; recovery runs on the file worker.
static void recover_tree(const char *root,bool backups,unsigned *repaired)
{
    struct frame {DIR *directory;char relative[DDS_PATH_MAX];};
    struct frame *frames=calloc(4,sizeof(*frames));if(!frames)return;
    frames[0].directory=opendir(root);if(!frames[0].directory){free(frames);return;}
    unsigned depth=0;
    while(1) {
        struct dirent *entry=readdir(frames[depth].directory);
        if(!entry) {
            closedir(frames[depth].directory);
            if(!depth)break;
            depth--;continue;
        }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        char path[DDS_PATH_MAX],file[DDS_PATH_MAX+80];
        int n=snprintf(path,sizeof(path),"%s%s%s",frames[depth].relative,depth?"/":"",entry->d_name);
        if(n<0 || n>=(int)sizeof(path))continue;
        full_path(file,sizeof(file),root,path,"");struct stat st;if(stat(file,&st))continue;
        if(S_ISDIR(st.st_mode)) {
            if(depth<3) {
                DIR *directory=opendir(file);
                if(directory){depth++;frames[depth].directory=directory;strcpy(frames[depth].relative,path);}
            }
            continue;
        }
        size_t len=strlen(path);const char *suffix=backups?".bak":".new";
        if(len<=4 || strcasecmp(path+len-4,suffix))continue;
        path[len-4]=0;if(!writable(path))continue;
        char error[DDS_ERROR_MAX];
        if(!backups) {
            if(valid_file(file,path,true)) {if(promote(root,path,file,error))(*repaired)++;}
            else if(remove_if_present(file))(*repaired)++;
        } else {
            char final[DDS_PATH_MAX+80];full_path(final,sizeof(final),root,path,"");
            if(!valid_file(final,path,false) && valid_file(file,path,false) && remove_if_present(final) && rename(file,final)==0)(*repaired)++;
        }
    }
    free(frames);
}
unsigned dds_files_recover(const char *root)
{
    unsigned n=0;recover_tree(root,false,&n);recover_tree(root,true,&n);return n;
}
bool dds_files_list(const char *root,unsigned offset,char *text,size_t size,unsigned *next,char *error)
{
    char folder[DDS_PATH_MAX+80];full_path(folder,sizeof(folder),root,"Patches/User","");
    DIR *cats=opendir(folder);if(!cats)return fail(error,"User storage unavailable");
    unsigned index=0,count=0;size_t used=0;bool ok=true,more=false;struct dirent *cat;
    text[0]=0;
    while(!more && (cat=readdir(cats))) {
        if(!dds_name_valid(cat->d_name))continue;
        char dir[DDS_PATH_MAX+160];snprintf(dir,sizeof(dir),"%s/%.64s",folder,cat->d_name);
        DIR *files=opendir(dir);if(!files)continue;struct dirent *entry;
        while((entry=readdir(files))) {
            if(strlen(entry->d_name)>69)continue;
            char path[DDS_PATH_MAX];int n=snprintf(path,sizeof(path),"Patches/User/%.64s/%.69s",cat->d_name,entry->d_name);
            if(n<0 || n>=(int)sizeof(path) || !dds_path_valid(path,true))continue;
            char file[DDS_PATH_MAX+240];snprintf(file,sizeof(file),"%s/%.69s",dir,entry->d_name);
            struct stat st;if(stat(file,&st)!=0 || !S_ISREG(st.st_mode))continue;
            if(index++<offset)continue;
            if(count==32 || used+(size_t)n+2>size){more=true;break;}
            memcpy(text+used,path,(size_t)n);used+=(size_t)n;text[used++]='\n';text[used]=0;count++;
        }
        closedir(files);
    }
    closedir(cats);*next=more?offset+count:0;return ok;
}
