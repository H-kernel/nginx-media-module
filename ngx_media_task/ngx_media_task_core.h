/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_MEDIA_TASK_CORE_H_INCLUDED_
#define _NGX_MEDIA_TASK_CORE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

extern ngx_module_t           ngx_task_module;

typedef struct {
    void                 *(*create_conf)(ngx_cycle_t *cycle);
    char                 *(*init_conf)(ngx_cycle_t *cycle, void *conf);
} ngx_task_module_t;

#define NGX_TASK_MODULE                 0x75534154     /* "TASK" */
#define NGX_TASK_CONF                   0x02000000

#define ngx_task_get_conf(cf, module)                                           \
    (((ngx_get_conf(cf->cycle->conf_ctx, ngx_task_module))                      \
    &&(*(ngx_get_conf(cf->cycle->conf_ctx, ngx_task_module))))?                 \
    (*(ngx_get_conf(cf->cycle->conf_ctx, ngx_task_module))) [module.ctx_index]: \
    NULL)

#define ngx_task_get_cycle_conf(cycle, module)                                  \
    (((ngx_get_conf(cycle->conf_ctx, ngx_task_module))                          \
    &&(*(ngx_get_conf(cycle->conf_ctx, ngx_task_module))))?                     \
    (*(ngx_get_conf(cycle->conf_ctx, ngx_task_module))) [module.ctx_index]:NULL)

#endif /* _NGX_MEDIA_TASK_CORE_H_INCLUDED_ */


