#include "ngx_media_worker_mediakernel.h"


#define NGG_HTTP_VIDEO_MK_TIME 5000


static void
ngx_media_worker_mk_timer(ngx_event_t *ev)
{
    int status = MK_TASK_STATUS_INIT;
    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ev->data;
    if(NULL == worker_ctx->watcher) {
        return;
    }
    int32_t  ret = mk_get_task_status(worker_ctx->run_handle,&status,&worker_ctx->worker_stat);
    if(MK_ERROR_CODE_OK != ret) {
        worker_ctx->watcher(ngx_media_worker_status_break,NGX_MEDIA_ERROR_CODE_RUN_TASK_ERROR,worker_ctx->wk_ctx);
        return;
    }

    ngx_int_t error_code = NGX_MEDIA_ERROR_CODE_OK;

    int32_t mk_last_error = mk_get_task_last_error(worker_ctx->run_handle);
    if(MK_ERROR_CODE_OK != mk_last_error ) {
        error_code = NGX_MEDIA_ERROR_CODE_RUN_TASK_ERROR;
    }
    if(MK_TASK_STATUS_INIT == status) {
        worker_ctx->watcher(ngx_media_worker_status_init,error_code,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUS_START == status) {
        worker_ctx->watcher(ngx_media_worker_status_start,error_code,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUS_RUNNING == status) {
        worker_ctx->watcher(ngx_media_worker_status_running,error_code,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUS_STOP == status) {
        worker_ctx->watcher(ngx_media_worker_status_end,error_code,worker_ctx->wk_ctx);
    }
    else {
        worker_ctx->watcher(ngx_media_worker_status_break,error_code,worker_ctx->wk_ctx);
    }

    if(MK_TASK_STATUS_STOP == status) {
        return;
    }
    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);
}



static ngx_int_t
ngx_media_worker_mk_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_uint_t lens = sizeof(ngx_worker_mk_ctx_t);
    ngx_worker_mk_ctx_t *worker_ctx = ngx_pcalloc(ctx->pool,lens);

    u_char mk_name[TRANS_STRING_MAX_LEN];

    ngx_memzero(&mk_name[0], TRANS_STRING_MAX_LEN);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mk_init begin");

    if(NULL == worker_ctx) {
       ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mk_init allocate mk ctx fail.");
        return NGX_ERROR;
    }

    ngx_media_worker_unescape_uri(ctx);

    ctx->priv_data_size = lens;
    ctx->priv_data      = worker_ctx;

    u_char* last = ngx_snprintf(mk_name,TRANS_STRING_MAX_LEN,"%V:%V", &ctx->taskid,&ctx->wokerid);
    *last = '\0';

    worker_ctx->run_handle = mk_create_handle((char*)&mk_name[0],MK_HANDLE_TYPE_TASK);
    worker_ctx->watcher    = watch;
    worker_ctx->wk_ctx     = ctx;
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_mk_release(ngx_media_worker_ctx_t* ctx)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_OK;
    }

    if(NULL == ctx->priv_data) {
        return NGX_OK;
    }
    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
    }
    ngx_pfree(ctx->pool, ctx->priv_data);
    ctx->priv_data      = NULL;
    ctx->priv_data_size = 0;
    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mk_start(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mk_start worker:[%V] begin.",&ctx->wokerid);
    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;

    int32_t ret  = mk_run_task(worker_ctx->run_handle,ctx->nparamcount,(char**)ctx->paramlist);

    if (MK_ERROR_CODE_OK != ret ) {
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
        ngx_log_error(NGX_LOG_WARN, ctx->log, 0,"ngx media worker mk start media kernel,  run the task fail,return:[%d].",ret);
        return NGX_ERROR;
    }

    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));
    worker_ctx->timer.handler = ngx_media_worker_mk_timer;
    worker_ctx->timer.log     = ctx->log;
    worker_ctx->timer.data    = worker_ctx;

    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mk_start worker:[%V] end.",&ctx->wokerid);

    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mk_stop(ngx_media_worker_ctx_t* ctx)
{

    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_stop_task(worker_ctx->run_handle);
    }
    //ngx_del_timer(&worker_ctx->timer);
    return NGX_OK;
}
static ngx_int_t    
ngx_media_worker_mk_control(ngx_media_worker_ctx_t* ctx,ngx_uint_t type,const char* name,const char* value)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_control_task(worker_ctx->run_handle,(uint32_t)type,(char*)name,(char*)value);
    }
    return NGX_OK;
}


/* media kernel worker */
ngx_media_worker_t ngx_video_mediakernel_worker = {
    .type             = ngx_media_worker_mediakernel,
    .init_worker      = ngx_media_worker_mk_init,
    .release_worker   = ngx_media_worker_mk_release,
    .start_worker     = ngx_media_worker_mk_start,
    .stop_worker      = ngx_media_worker_mk_stop,
    .control_worker   = ngx_media_worker_mk_control,
};



