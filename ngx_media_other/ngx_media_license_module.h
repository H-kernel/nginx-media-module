/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_MEDIA_LICENSE_H_INCLUDED_
#define _NGX_MEDIA_LICENSE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

extern ngx_module_t           ngx_license_module;

typedef struct {
    void                 *(*create_conf)(ngx_cycle_t *cycle);
    char                 *(*init_conf)(ngx_cycle_t *cycle, void *conf);
} ngx_license_module_t;

#define NGX_LICENSE_MODULE                 0x5345434C     /* "LCES" */
#define NGX_LICENSE_CONF                   0x02000000

#define ngx_license_get_conf(cf, module)                                           \
      (ngx_get_conf(cf->cycle->conf_ctx, ngx_license_module))                      \

#define ngx_license_get_cycle_conf(cycle, module)                                  \
      (ngx_get_conf(cycle->conf_ctx, ngx_license_module))                          \

ngx_uint_t ngx_media_license_task_max();
void       ngx_media_license_task_inc();
void       ngx_media_license_task_dec();
ngx_uint_t ngx_media_license_task_current();

ngx_uint_t ngx_media_license_rtmp_channle();
void       ngx_media_license_rtmp_inc();
void       ngx_media_license_rtmp_dec();
ngx_uint_t ngx_media_license_rtmp_current();

ngx_uint_t ngx_media_license_rtsp_channle();
void       ngx_media_license_rtsp_inc();
void       ngx_media_license_rtsp_dec();
ngx_uint_t ngx_media_license_rtsp_current();

ngx_uint_t ngx_media_license_hls_channle();
void       ngx_media_license_hls_inc();
void       ngx_media_license_hls_dec();
ngx_uint_t ngx_media_license_hls_current();

ngx_str_t* ngx_media_license_file_path();




#endif /* _NGX_MEDIA_LICENSE_H_INCLUDED_ */

