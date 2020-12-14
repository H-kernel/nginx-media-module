#ifndef __NGX_MEDIA_WORKER_TRANSFER_H__
#define __NGX_MEDIA_WORKER_TRANSFER_H__
#include "ngx_media_worker.h"

static ngx_int_t    ngx_media_worker_transfer_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);
static ngx_int_t    ngx_media_worker_transfer_release(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_transfer_start(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_transfer_stop(ngx_media_worker_ctx_t* ctx);
#endif /*__NGX_MEDIA_WORKER_TRANSFER_H__*/

