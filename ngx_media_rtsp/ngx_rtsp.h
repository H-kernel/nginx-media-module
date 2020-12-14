
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Winshining
 */


#ifndef _NGX_RTSP_H_INCLUDED_
#define _NGX_RTSP_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
#include <libasrtspsvr/libRtspSvr.h>

extern ngx_module_t                         ngx_rtsp_core_module;

typedef struct ngx_rtsp_core_srv_conf_s  ngx_rtsp_core_srv_conf_t;


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif


typedef struct {
    void                  **main_conf;
    void                  **srv_conf;
    void                  **app_conf;
} ngx_rtsp_conf_ctx_t;

typedef struct {
    ngx_array_t              servers;    /* ngx_rtsp_core_srv_conf_t */
} ngx_rtsp_core_main_conf_t;

struct ngx_rtsp_core_srv_conf_s {
    ngx_array_t             applications; /* ngx_rtsp_core_app_conf_t */
    ngx_rtsp_conf_ctx_t    *ctx;
    ngx_uint_t              port;         /* rtsp listen port */
    AS_HANDLE              *handle;       /* rtsp server handle */
};

typedef struct {
    ngx_array_t               applications; /* ngx_rtsp_core_app_conf_t */
    ngx_str_t                 name;
    void                    **app_conf;
    AS_HANDLE                *handle;       /* rtsp server handle */
} ngx_rtsp_core_app_conf_t;


typedef struct {
    ngx_int_t             (*preconfiguration)(ngx_conf_t *cf);
    ngx_int_t             (*postconfiguration)(ngx_conf_t *cf);

    void                 *(*create_main_conf)(ngx_conf_t *cf);
    char                 *(*init_main_conf)(ngx_conf_t *cf, void *conf);

    void                 *(*create_srv_conf)(ngx_conf_t *cf);
    char                 *(*merge_srv_conf)(ngx_conf_t *cf, void *prev, void *conf);

    void                 *(*create_app_conf)(ngx_conf_t *cf);
    char                 *(*merge_app_conf)(ngx_conf_t *cf, void *prev, void *conf);
} ngx_rtsp_module_t;

#define NGX_RTSP_MODULE                 0x50535452     /* "RTSP" */

#define NGX_RTSP_MAIN_CONF              0x02000000
#define NGX_RTSP_SRV_CONF               0x04000000
#define NGX_RTSP_APP_CONF               0x08000000


#define NGX_RTSP_MAIN_CONF_OFFSET  offsetof(ngx_rtsp_conf_ctx_t, main_conf)
#define NGX_RTSP_SRV_CONF_OFFSET   offsetof(ngx_rtsp_conf_ctx_t, srv_conf)
#define NGX_RTSP_APP_CONF_OFFSET   offsetof(ngx_rtsp_conf_ctx_t, app_conf)


#define ngx_rtsp_conf_get_module_main_conf(cf, module)                       \
    ((ngx_rtsp_conf_ctx_t *) cf->ctx)->main_conf[module.ctx_index]
#define ngx_rtsp_conf_get_module_srv_conf(cf, module)                        \
    ((ngx_rtsp_conf_ctx_t *) cf->ctx)->srv_conf[module.ctx_index]
#define ngx_rtsp_conf_get_module_app_conf(cf, module)                        \
    ((ngx_rtsp_conf_ctx_t *) cf->ctx)->app_conf[module.ctx_index]

#define ngx_rtsp_cycle_get_module_main_conf(cycle, module)                    \
    (cycle->conf_ctx[ngx_rtsp_module.index] ?                                 \
        ((ngx_rtsp_conf_ctx_t *) cycle->conf_ctx[ngx_rtsp_module.index])      \
            ->main_conf[module.ctx_index]:                                    \
        NULL)

#endif /* _NGX_RTSP_H_INCLUDED_ */
