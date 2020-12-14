
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Winshining
 */


#ifndef _NGX_SCHD_H_INCLUDED_
#define _NGX_SCHD_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>

#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

extern ngx_module_t                 ngx_schd_core_module;
extern ngx_module_t                 ngx_schd_module;


typedef struct {
    ngx_str_t                       sch_zk_server_id;
    ngx_uint_t                      sch_server_flags;
    ngx_keyval_t                    sch_signal_ip;
    ngx_keyval_t                    sch_service_ip;
    ngx_array_t                    *sch_disk;
} ngx_schd_core_conf_t;





typedef struct {
    void                 *(*create_conf)(ngx_cycle_t *cycle);
    char                 *(*init_conf)(ngx_cycle_t *cycle, void *conf);
} ngx_schd_module_t;

#define NGX_SCHD_MODULE                 0x44484353     /* "SCHD" */
#define NGX_SCHD_CONF                   0x02000000

#define ngx_schd_get_conf(cf, module)                                           \
    (((ngx_get_conf(cf->cycle->conf_ctx, ngx_schd_module))                      \
    &&(*(ngx_get_conf(cf->cycle->conf_ctx, ngx_schd_module))))?                 \
    (*(ngx_get_conf(cf->cycle->conf_ctx, ngx_schd_module))) [module.ctx_index]: \
    NULL)

#define ngx_schd_get_cycle_conf(cycle, module)                                  \
    (((ngx_get_conf(cycle->conf_ctx, ngx_schd_module))                          \
    &&(*(ngx_get_conf(cycle->conf_ctx, ngx_schd_module))))?                     \
    (*(ngx_get_conf(cycle->conf_ctx, ngx_schd_module))) [module.ctx_index]:NULL)


#endif /* _NGX_SCHD_H_INCLUDED_ */
