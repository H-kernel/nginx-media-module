#ifndef __NGX_MEDIA_WORKER_MSS_H__
#define __NGX_MEDIA_WORKER_MSS_H__
#include "ngx_media_worker.h"
#include "libMediaKenerl.h"
#include "mk_def.h"

static ngx_int_t    ngx_media_worker_mss_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);
static ngx_int_t    ngx_media_worker_mss_release(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mss_start(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mss_stop(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_mss_control(ngx_media_worker_ctx_t* ctx,ngx_uint_t type,const char* name,const char* value);
#endif /*__NGX_MEDIA_WORKER_MSS_H__*/