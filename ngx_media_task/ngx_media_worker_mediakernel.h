#ifndef __NGX_MEDIA_WORKER_MEDIAKERNEL_H__
#define __NGX_MEDIA_WORKER_MEDIAKERNEL_H__
#include "ngx_media_worker.h"
#include "libMediaKenerl.h"
#include "mk_def.h"

typedef struct {
    MK_HANDLE                       run_handle;
    mk_task_stat_info_t             worker_stat;
    WK_WATCH                        watcher;
    ngx_media_worker_ctx_t         *wk_ctx;
    ngx_event_t                     timer;
} ngx_worker_mk_ctx_t;

static ngx_int_t    ngx_media_worker_mk_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);
static ngx_int_t    ngx_media_worker_mk_release(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mk_start(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mk_stop(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mk_control(ngx_media_worker_ctx_t* ctx,ngx_uint_t type,const char* name,const char* value);
#endif /*__NGX_MEDIA_WORKER_MEDIAKERNEL_H__*/