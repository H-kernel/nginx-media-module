#ifndef _NGX_MEDIA_SYS_STAT_H__
#define _NGX_MEDIA_SYS_STAT_H__
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
#include <ngx_http.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

/********************* function begin *********************/
ngx_int_t  ngx_media_sys_stat_init(ngx_cycle_t *cycle);

void       ngx_media_sys_stat_release(ngx_cycle_t *cycle);

ngx_int_t  ngx_media_sys_stat_add_networdcard(u_char* strIP);

ngx_int_t  ngx_media_sys_stat_remove_networdcard(u_char* strIP);

ngx_int_t  ngx_media_sys_stat_add_disk(u_char* strDiskName,u_char* strDiskPath);

ngx_int_t  ngx_media_sys_stat_remove_disk(u_char* strDiskPath);

ngx_uint_t ngx_media_sys_stat_get_all_disk(u_char** diskNameArray,u_char** diskPathArray,ngx_uint_t max);

void       ngx_media_sys_stat_get_cpuinfo(ngx_uint_t *ulUsedPer);

void       ngx_media_sys_stat_get_memoryinfo(ngx_uint_t* ulTotalSize, ngx_uint_t* ulUsedSize);

ngx_int_t  ngx_media_sys_stat_get_networkcardinfo(u_char* strIP, ngx_uint_t* ulTotalSize,
                   ngx_uint_t* ulUsedRecvSize, ngx_uint_t* ulUsedSendSize);

ngx_int_t  ngx_media_sys_stat_get_diskinfo(u_char* strDiskPath, ngx_uint_t* ullTotalSize, ngx_uint_t* ullUsedSize);

u_char*    ngx_media_sys_map_uri_to_path(ngx_http_request_t *r, ngx_str_t *path,size_t *root_length, size_t reserved);

u_char*    ngx_media_sys_map_vfile_to_path(ngx_http_request_t *r, ngx_str_t *path,ngx_str_t* vfile);

u_char*    ngx_media_sys_map_vpath_vfile_to_path(ngx_http_request_t *r,ngx_str_t* vpath,ngx_str_t* vfile,ngx_str_t *path);

#endif


