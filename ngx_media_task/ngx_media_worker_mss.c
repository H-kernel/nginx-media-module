#include "ngx_media_worker_mss.h"
#include <ngx_http.h>
#include <curl/curl.h>
#include <common/as_json.h>


#define MSS_WORK_ARG_PREF            "-mss_"
#define MSS_WORK_ARG_ADDR            "-mss_addr"
#define MSS_WORK_ARG_CAMERAID        "-mss_cameraid"
#define MSS_WORK_ARG_TYPE            "-mss_type"
#define MSS_WORK_ARG_STREAMTYPE      "-mss_streamtype"
#define MSS_WORK_ARG_LOCATION        "-mss_location"
#define MSS_WORK_ARG_CONTENTID       "-mss_contentId"
#define MSS_WORK_ARG_STARTTIME       "-mss_starttime"
#define MSS_WORK_ARG_ENDTIME         "-mss_endtime"
#define MSS_WORK_ARG_USERNAME        "-mss_username"
#define MSS_WORK_ARG_PASSWORD        "-mss_password"
#define MSS_WORK_ARG_AGENT_TYPE      "-mss_agenttype"
#define MSS_WORK_ARG_NVRCODE         "-mss_nvrcode"

#define MSS_WORK_ARG_ANALYZEDURATION "-mss_analyzeduration"
#define MSS_WORK_ARG_THREADS         "-mss_threads"



#define MSS_WORK_MEDIA_TYPE_LIVE     "live"
#define MSS_WORK_MEDIA_TYPE_PLAYBACK "record"
#define MSS_WORK_MEDIA_TYPE_DOWNLOAD "download"


typedef enum
{
    MSS_MEDIA_TYE_LIVE      = 0,
    MSS_MEDIA_TYE_PLAYBACK  = 1,
    MSS_MEDIA_TYE_DOWNLOAD  = 2,
    MSS_MEDIA_TYE_MAX
}MSS_MEDIA_TYE;

typedef enum NGX_MEDIA_WOKER_MSS_STATUS
{
    NGX_MEDIA_WOKER_MSS_STATUS_INIT    = 0,
    NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL = 1,
    NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN  = 2,
    NGX_MEDIA_WOKER_MSS_STATUS_BREAK   = 3,
    NGX_MEDIA_WOKER_MSS_STATUS_END
}WOKER_MSS_STATUS;

typedef struct {
    ngx_str_t                       mss_addr;
    ngx_str_t                       mss_name;
    ngx_str_t                       mss_passwd;
    ngx_str_t                       mss_cameraid;
    ngx_str_t                       mss_type;
    ngx_str_t                       mss_location;
    ngx_str_t                       mss_streamtype;   /* for live */
    ngx_str_t                       mss_contentid;
    ngx_str_t                       mss_starttime;
    ngx_str_t                       mss_endtime;
    ngx_str_t                       mss_agenttype;
    ngx_str_t                       mss_nvrcode;

    ngx_str_t                       mss_analyzed_duration;
    ngx_str_t                       mss_threads;
} ngx_worker_mss_arg_t;

typedef struct {
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    WK_WATCH                        watcher;
    ngx_media_worker_ctx_t         *wk_ctx;
    ngx_event_t                     timer;
    WOKER_MSS_STATUS                status;
    /* mss http request */
    ngx_worker_mss_arg_t            mss_arg;
    CURL                           *mss_req;
    struct curl_slist              *mss_headList;
    ngx_str_t                       mss_req_msg;
    ngx_str_t                       mss_resp_msg;
    /* mk context */
    MK_HANDLE                       run_handle;
    mk_task_stat_info_t             worker_stat;
    ngx_int_t                       mk_nparamcount;
    ngx_int_t                       mk_srcIndex;                      
    u_char**                        mk_paramlist;
} ngx_worker_mss_ctx_t;


#define NGX_HTTP_VIDEO_MSS_TIME  2000
#define NGX_MSS_ERROR_CODE_OK    "00000000"
#define NGX_MSS_RESPONSE_MAX_LEN 2048

enum NGX_MEDIA_WORKER_MSS_HTTP_HEADER
{
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE    = 0,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT  = 1,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH    = 2,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX
};

static ngx_int_t      ngx_media_worker_mss_url_request(ngx_worker_mss_ctx_t *ctx);


static void
ngx_media_worker_mss_args(ngx_worker_mss_ctx_t* mss_ctx,u_char* arg,u_char* value,ngx_int_t* index,ngx_uint_t set)
{
    ngx_int_t ret = NGX_OK;
    size_t size = ngx_strlen(arg);
    if(ngx_strncmp(arg,MSS_WORK_ARG_ADDR,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_addr);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_CAMERAID,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_cameraid);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_TYPE,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_type);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_LOCATION,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_location);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_STREAMTYPE,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_streamtype);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_CONTENTID,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_contentid);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_STARTTIME,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_starttime);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_ENDTIME,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_endtime);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_USERNAME,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_name);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_PASSWORD,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_passwd);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_AGENT_TYPE,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_agenttype);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_NVRCODE,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_nvrcode);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_ANALYZEDURATION,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_analyzed_duration);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_THREADS,size) == 0) {
        if(set) {
            ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_threads);
        }
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else {
        return;
    }
    return ;
}


static ngx_int_t
ngx_media_worker_mss_parser_args(ngx_media_worker_ctx_t* ctx)
{
    ngx_worker_mss_ctx_t* mss_ctx = (ngx_worker_mss_ctx_t*)ctx->priv_data;

    ngx_int_t i = 0;
    u_char* arg   = NULL;
    u_char* value = NULL;

    /*1.parser the mss_ args */
    for(i = 0; i < ctx->nparamcount;i++) {
        arg   = ctx->paramlist[i];
        value = NULL;
        if(i < (ctx->nparamcount -1)) {
            value = ctx->paramlist[i+1];
        }

        if(ngx_strncmp(arg,MSS_WORK_ARG_PREF,ngx_strlen(MSS_WORK_ARG_PREF)) == 0) {
            ngx_media_worker_mss_args(mss_ctx,arg,value,&i,1);
        }
    }

    ngx_uint_t lens  = sizeof(u_char*)*(ctx->nparamcount + 32);

    mss_ctx->mk_paramlist    = ngx_pcalloc(ctx->pool,lens);
    mss_ctx->mk_nparamcount  = 0;

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-reorder_queue_size";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"10000";
    mss_ctx->mk_nparamcount++;

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-buffer_size";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"2048000";
    mss_ctx->mk_nparamcount++;

    if(NULL == mss_ctx->mss_arg.mss_type.data) {
        return NGX_ERROR;
    }
    u_char* analyzeduration = (u_char*)"5000000";
    if((NULL != mss_ctx->mss_arg.mss_analyzed_duration.data)&&(0 < mss_ctx->mss_arg.mss_analyzed_duration.len)) {
        analyzeduration =  mss_ctx->mss_arg.mss_analyzed_duration.data;
    }
    else {
        if(ngx_strncmp(mss_ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_LIVE,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_LIVE)) == 0) {
            analyzeduration = (u_char*)"2000000";
        }
        else if(ngx_strncmp(mss_ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_PLAYBACK,
                                    ngx_strlen(MSS_WORK_MEDIA_TYPE_PLAYBACK)) == 0) {
            analyzeduration = (u_char*)"5000000";
        }
        else if(ngx_strncmp(mss_ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_DOWNLOAD,
                                    ngx_strlen(MSS_WORK_MEDIA_TYPE_DOWNLOAD)) == 0) {
            analyzeduration = (u_char*)"5000000";
        }
    }

    

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-analyzeduration";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = analyzeduration;
    mss_ctx->mk_nparamcount++;

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-rtsp_transport";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"tcp";
    mss_ctx->mk_nparamcount++;

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-stimeout";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"5000000";
    mss_ctx->mk_nparamcount++;

    u_char* threads = (u_char*)"1";
    if((NULL != mss_ctx->mss_arg.mss_threads.data)&&(0 < mss_ctx->mss_arg.mss_threads.len)) {
        threads =  mss_ctx->mss_arg.mss_threads.data;
    }

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-threads";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = threads;
    mss_ctx->mk_nparamcount++;

    if(ngx_strncmp(mss_ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_DOWNLOAD,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_DOWNLOAD)) == 0) {
        mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-scale";
        mss_ctx->mk_nparamcount++;
        mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"255";
        mss_ctx->mk_nparamcount++;
    }

    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"-src";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = (u_char*)"tmp"; /* replace by the mms return rtsp url */
    mss_ctx->mk_srcIndex  = mss_ctx->mk_nparamcount;
    mss_ctx->mk_nparamcount++;

    for(i = 0; i < ctx->nparamcount;i++) {
        arg   = ctx->paramlist[i];
        value = NULL;
        if(i < (ctx->nparamcount -1)) {
            value = ctx->paramlist[i+1];
        }

        if(ngx_strncmp(arg,MSS_WORK_ARG_PREF,ngx_strlen(MSS_WORK_ARG_PREF)) == 0) {
            ngx_media_worker_mss_args(mss_ctx,arg,value,&i,0);/* skip the mss_ args */
        }
        else
        {
            mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = arg;
            mss_ctx->mk_nparamcount++;
        }
    }
    return NGX_OK;
}

static void
ngx_media_worker_mss_timer(ngx_event_t *ev)
{
    int status = MK_TASK_STATUS_INIT;
    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ev->data;
    ngx_media_worker_ctx_t* ctx      = worker_ctx->wk_ctx;
    ngx_int_t error_code = NGX_MEDIA_ERROR_CODE_OK;

    if(NULL == worker_ctx->watcher) {
        return;
    }

    if(NGX_MEDIA_WOKER_MSS_STATUS_INIT == worker_ctx->status ) {
        worker_ctx->watcher(ngx_media_worker_status_init,NGX_MEDIA_ERROR_CODE_OK,worker_ctx->wk_ctx);
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL;
        ngx_add_timer(&worker_ctx->timer,1000);
        return;
    }
    else if(NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL == worker_ctx->status ) {
        if(NGX_OK != ngx_media_worker_mss_url_request(worker_ctx)) {
            ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_timer worker:[%V] send mss request fail.",&ctx->wokerid);
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
            worker_ctx->watcher(ngx_media_worker_status_break,NGX_MEDIA_ERROR_CODE_RUN_TASK_ERROR,worker_ctx->wk_ctx);
            return;
        }
        worker_ctx->watcher(ngx_media_worker_status_running,NGX_MEDIA_ERROR_CODE_OK,worker_ctx->wk_ctx);
    }
    else if(NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN == worker_ctx->status ) {
        int32_t  ret = mk_get_task_status(worker_ctx->run_handle,&status,&worker_ctx->worker_stat);
        if(MK_ERROR_CODE_OK != ret) {
            return;
        }
        /*
        int32_t mk_last_error = mk_get_task_last_error(worker_ctx->run_handle);
        if(MK_ERROR_CODE_OK != mk_last_error ) {
            error_code = NGX_MEDIA_ERROR_CODE_RUN_TASK_ERROR;
        }
        */
        ngx_log_error(NGX_LOG_INFO, ctx->log, 0,
                              "ngx_media_worker_mss_timer worker:[%V] get mk status:[%d].",&ctx->wokerid,status);
        if(MK_TASK_STATUS_INIT == status) {
            worker_ctx->watcher(ngx_media_worker_status_running,error_code,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUS_START == status) {
            worker_ctx->watcher(ngx_media_worker_status_running,error_code,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUS_RUNNING == status) {
            worker_ctx->watcher(ngx_media_worker_status_running,error_code,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUS_STOP == status) {
            worker_ctx->watcher(ngx_media_worker_status_end,error_code,worker_ctx->wk_ctx);
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_END;
        }
        else {
            worker_ctx->watcher(ngx_media_worker_status_break,error_code,worker_ctx->wk_ctx);
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        }

    }
    else if(NGX_MEDIA_WOKER_MSS_STATUS_END == worker_ctx->status) {
        worker_ctx->watcher(ngx_media_worker_status_end,error_code,worker_ctx->wk_ctx);
    }

    if(NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN < worker_ctx->status) {
        return;
    }
    ngx_add_timer(&worker_ctx->timer,NGX_HTTP_VIDEO_MSS_TIME);
}

static ngx_uint_t
ngx_media_worker_mss_start_media_kernel(ngx_worker_mss_ctx_t *worker_ctx)
{
    ngx_flag_t flag = 0;
    ngx_media_worker_ctx_t* ctx = worker_ctx->wk_ctx;
    u_char mk_name[TRANS_STRING_MAX_LEN];

    ngx_memzero(&mk_name[0], TRANS_STRING_MAX_LEN);

    /* 1.parse the response */
    cJSON* root = cJSON_Parse((char*)worker_ctx->mss_resp_msg.data);
    if (NULL == root) {
        ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message parser fail.");
        return NGX_ERROR;
    }
    do {
        cJSON *resultCode = cJSON_GetObjectItem(root, "resultCode");
        if(NULL == resultCode) {
            ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message there is no resultCode.");
            break;
        }
        if(resultCode->type == cJSON_String) {
            if(0 != ngx_strncmp(NGX_MSS_ERROR_CODE_OK,resultCode->valuestring,ngx_strlen(NGX_MSS_ERROR_CODE_OK))) {
                ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, "
                                                            "camera:[%V], resultCode:[%s] is not success.",
                                                            &worker_ctx->mss_arg.mss_cameraid,
                                                            resultCode->valuestring);
                break;
            }
        }
        else if (resultCode->type == cJSON_Number) {
            if(0 != resultCode->valueint) {
                ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, "
                                                            "camera:[%V], resultCode:[%d] is not success.",
                                                            &worker_ctx->mss_arg.mss_cameraid,
                                                            resultCode->valueint);
                break;
            }
        }
        else 
        {
            ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, "
                                                            "camera:[%V], resultCode json object:[%d] unknow.",
                                                            &worker_ctx->mss_arg.mss_cameraid,
                                                            resultCode->type);
            break;
        }

        cJSON *url = cJSON_GetObjectItem(root, "url");
        if(NULL == url) {
            ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message there is no url.");
            break;
        }

        ngx_uint_t lens  = ngx_strlen(url->valuestring);
        worker_ctx->mk_paramlist[worker_ctx->mk_srcIndex] = ngx_pcalloc(worker_ctx->pool,lens + 1);
        u_char* last = ngx_cpymem(worker_ctx->mk_paramlist[worker_ctx->mk_srcIndex], url->valuestring,lens);
        *last = '\0';
        flag = 1;
        ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"ngx media worker mss start media kernel,camera:[%V],the url:[%s].",
                                                       &worker_ctx->mss_arg.mss_cameraid,
                                                        worker_ctx->mk_paramlist[worker_ctx->mk_srcIndex]);
    }while(0);
    cJSON_Delete(root);

    if(!flag) {
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        return NGX_ERROR;
    }

    /* 2. start the MK task */
    u_char* last = ngx_snprintf(mk_name,TRANS_STRING_MAX_LEN,"%V:%V", &ctx->taskid,&ctx->wokerid);
    *last = '\0';

    worker_ctx->run_handle = mk_create_handle((char*)&mk_name[0],MK_HANDLE_TYPE_TASK);
    int32_t ret  = mk_run_task(worker_ctx->run_handle,worker_ctx->mk_nparamcount,(char**)worker_ctx->mk_paramlist);

    if (MK_ERROR_CODE_OK != ret ) {
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        ngx_log_error(NGX_LOG_WARN, worker_ctx->log, 0,"ngx media worker mss start media kernel, "
                                                           "camera:[%V], run the task fail,return:[%d].",
                                                           &worker_ctx->mss_arg.mss_cameraid,ret);
        return NGX_ERROR;
    }
    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN;
    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mss_create_req_msg(ngx_worker_mss_ctx_t *ctx)
{
    ngx_uint_t lens  = 0;
    ngx_int_t  ret   = NGX_ERROR;
    MSS_MEDIA_TYE enMediaType = MSS_MEDIA_TYE_LIVE;

    if(ngx_strncmp(ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_LIVE,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_LIVE)) == 0) {
        enMediaType = MSS_MEDIA_TYE_LIVE;
    }
    else if(ngx_strncmp(ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_PLAYBACK,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_PLAYBACK)) == 0) {
        enMediaType = MSS_MEDIA_TYE_PLAYBACK;
    }
    else if(ngx_strncmp(ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_DOWNLOAD,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_DOWNLOAD)) == 0) {
        enMediaType = MSS_MEDIA_TYE_DOWNLOAD;
    }
    else {
        return NGX_ERROR;
    }

    cJSON* root = cJSON_CreateObject();
    if(NULL == root) {
        return NGX_ERROR;
    }
    do {
        if(MSS_MEDIA_TYE_LIVE == enMediaType) {

            if((NULL != ctx->mss_arg.mss_cameraid.data)&&(0 < ctx->mss_arg.mss_cameraid.len)) {
                cJSON_AddItemToObject(root, "cameraId", cJSON_CreateString((char*)ctx->mss_arg.mss_cameraid.data));
            }

            if((NULL != ctx->mss_arg.mss_streamtype.data)&&(0 < ctx->mss_arg.mss_streamtype.len)) {
                cJSON_AddItemToObject(root, "streamType", cJSON_CreateString((char*)ctx->mss_arg.mss_streamtype.data));
            }

            cJSON_AddItemToObject(root, "urlType", cJSON_CreateString("1"));

            if((NULL != ctx->mss_arg.mss_agenttype.data)&&(0 < ctx->mss_arg.mss_agenttype.len)) {
                cJSON_AddItemToObject(root, "agentType", cJSON_CreateString((char*)ctx->mss_arg.mss_agenttype.data));
            }
        }
        else if((MSS_MEDIA_TYE_PLAYBACK == enMediaType)||(MSS_MEDIA_TYE_DOWNLOAD == enMediaType)) {

            if((NULL != ctx->mss_arg.mss_cameraid.data)&&(0 < ctx->mss_arg.mss_cameraid.len)) {
                cJSON_AddItemToObject(root, "cameraId", cJSON_CreateString((char*)ctx->mss_arg.mss_cameraid.data));
            }

            if((NULL != ctx->mss_arg.mss_streamtype.data)&&(0 < ctx->mss_arg.mss_streamtype.len)) {
                cJSON_AddItemToObject(root, "streamType", cJSON_CreateString((char*)ctx->mss_arg.mss_streamtype.data));
            }

            cJSON_AddItemToObject(root, "urlType", cJSON_CreateString("1"));

            if((NULL != ctx->mss_arg.mss_agenttype.data)&&(0 < ctx->mss_arg.mss_agenttype.len)) {
                cJSON_AddItemToObject(root, "agentType", cJSON_CreateString((char*)ctx->mss_arg.mss_agenttype.data));
            }
            if(MSS_MEDIA_TYE_PLAYBACK == enMediaType) {
                cJSON_AddItemToObject(root, "vodType", cJSON_CreateString("vod"));
            }
            else if(MSS_MEDIA_TYE_DOWNLOAD == enMediaType) {
                cJSON_AddItemToObject(root, "vodType", cJSON_CreateString("download"));
            }
            cJSON* vodInfo = cJSON_CreateObject();

            if((NULL != ctx->mss_arg.mss_contentid.data)&&(0 < ctx->mss_arg.mss_contentid.len)) {
                cJSON_AddItemToObject(vodInfo, "contentId", cJSON_CreateString((char*)ctx->mss_arg.mss_contentid.data));
            }

            if((NULL != ctx->mss_arg.mss_cameraid.data)&&(0 < ctx->mss_arg.mss_cameraid.len)) {
                cJSON_AddItemToObject(vodInfo, "cameraId", cJSON_CreateString((char*)ctx->mss_arg.mss_cameraid.data));
            }
            
            if((NULL != ctx->mss_arg.mss_starttime.data)&&(0 < ctx->mss_arg.mss_starttime.len)) {
                cJSON_AddItemToObject(vodInfo, "beginTime", cJSON_CreateString((char*)ctx->mss_arg.mss_starttime.data));
            }

            if((NULL != ctx->mss_arg.mss_endtime.data)&&(0 < ctx->mss_arg.mss_endtime.len)) {
               cJSON_AddItemToObject(vodInfo, "endTime", cJSON_CreateString((char*)ctx->mss_arg.mss_endtime.data));
            }
            
            if((NULL != ctx->mss_arg.mss_location.data)&&(0 < ctx->mss_arg.mss_location.len)) {
               cJSON_AddItemToObject(vodInfo, "location", cJSON_CreateString((char*)ctx->mss_arg.mss_location.data));
            }

            if((NULL != ctx->mss_arg.mss_nvrcode.data)&&(0 < ctx->mss_arg.mss_nvrcode.len)) {
               cJSON_AddItemToObject(vodInfo, "nvrCode", cJSON_CreateString((char*)ctx->mss_arg.mss_nvrcode.data));
            }
            cJSON_AddItemToObject(root, "vodInfo",vodInfo);
        }
        char* msg = cJSON_PrintUnformatted(root);
        lens = ngx_strlen(msg);
        ctx->mss_req_msg.data = ngx_pcalloc(ctx->pool,lens + 1);
        ctx->mss_req_msg.len = lens;
        ngx_memcpy(ctx->mss_req_msg.data, msg,lens);
        free(msg);
        ret   = NGX_OK;
    }while(0);

    cJSON_Delete(root);
    return ret;
}


static size_t
ngx_media_worker_mss_curl_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ngx_worker_mss_ctx_t *ctx = (ngx_worker_mss_ctx_t*)userdata;
    if(NULL == ctx->mss_resp_msg.data) {
        return NGX_ERROR;
    }

    size_t recv_size = size * nmemb;

    ngx_uint_t recved_size = ngx_strlen(ctx->mss_resp_msg.data);
    if(NGX_MSS_RESPONSE_MAX_LEN < (recv_size + recved_size)) {
        return NGX_ERROR;
    }
    u_char*last = ngx_cpymem(ctx->mss_resp_msg.data + recved_size, ptr, recv_size);

    *last = '\0';

    return recv_size;
}


static ngx_int_t
ngx_media_worker_mss_url_request(ngx_worker_mss_ctx_t *ctx)
{
    /* create the message body */
    if(0 == ctx->mss_req_msg.len) {
        if(NGX_OK != ngx_media_worker_mss_create_req_msg(ctx)) {
            ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] create mss request message fail.",
                              &ctx->wk_ctx->wokerid);
            return NGX_ERROR;
        }
    }

    if(0 == ctx->mss_resp_msg.len) {
        ctx->mss_resp_msg.data = ngx_pcalloc(ctx->pool,NGX_MSS_RESPONSE_MAX_LEN);
        ctx->mss_resp_msg.len  = NGX_MSS_RESPONSE_MAX_LEN;
        if(NULL == ctx->mss_resp_msg.data) {
            ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] create mss response message fail.",
                              &ctx->wk_ctx->wokerid);
            return NGX_ERROR;
        }
    }

    ctx->mss_req = curl_easy_init();;
    if(NULL == ctx->mss_req) {
        ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] curl init fail.",
                              &ctx->wk_ctx->wokerid);
        return NGX_ERROR;
    }
    curl_easy_setopt(ctx->mss_req, CURLOPT_USERAGENT, "alltask/1.0");
	curl_easy_setopt(ctx->mss_req, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(ctx->mss_req, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(ctx->mss_req, CURLOPT_TIMEOUT, 10);

	ctx->mss_headList = curl_slist_append(NULL, "Content-Type:application/json;charset=UTF-8");
	curl_easy_setopt(ctx->mss_req, CURLOPT_HTTPHEADER, ctx->mss_headList);

	curl_easy_setopt(ctx->mss_req, CURLOPT_WRITEFUNCTION, ngx_media_worker_mss_curl_callback);
	curl_easy_setopt(ctx->mss_req, CURLOPT_WRITEDATA, ctx);
    if((0 < ctx->mss_arg.mss_name.len)&&(0 < ctx->mss_arg.mss_passwd.len)) {
        curl_easy_setopt(ctx->mss_req, CURLOPT_USERNAME, ctx->mss_arg.mss_name.data);
	    curl_easy_setopt(ctx->mss_req, CURLOPT_PASSWORD, ctx->mss_arg.mss_passwd.data);
    }
    curl_easy_setopt(ctx->mss_req, CURLOPT_URL, ctx->mss_arg.mss_addr.data);

    curl_easy_setopt(ctx->mss_req, CURLOPT_POSTFIELDS, (char*)ctx->mss_req_msg.data);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] ,request:\n%V.",
                              &ctx->wk_ctx->wokerid,&ctx->mss_req_msg);

    do
    {
        CURLcode eResult = curl_easy_perform(ctx->mss_req);
    	if (CURLE_OK != eResult) {
            ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] camera:[%V]"
                              "curl perform fail,code:[%d],result:[%s].",
                              &ctx->wk_ctx->wokerid,&ctx->mss_arg.mss_cameraid,
                              eResult,curl_easy_strerror(eResult));
    		break;
    	}

        long nResponseCode;
    	curl_easy_getinfo(ctx->mss_req, CURLINFO_RESPONSE_CODE, &nResponseCode);

    	if (NGX_HTTP_OK != nResponseCode) {
            ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                              "ngx_media_worker_mss_url_request, worker:[%V] HTTP response is not 200 OK,code:[%d].",
                              &ctx->wk_ctx->wokerid,nResponseCode);
    		break;
    	}

        if (NULL != ctx->mss_headList) {
    		curl_slist_free_all(ctx->mss_headList);
            ctx->mss_headList = NULL;
    	}

    	if (NULL != ctx->mss_req) {
    		curl_easy_cleanup(ctx->mss_req);
    		ctx->mss_req = NULL;
    	}

        return ngx_media_worker_mss_start_media_kernel(ctx);
    }while(0);

    ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;

    if (NULL != ctx->mss_headList) {
		curl_slist_free_all(ctx->mss_headList);
        ctx->mss_headList = NULL;
	}

	if (NULL != ctx->mss_req) {
		curl_easy_cleanup(ctx->mss_req);
		ctx->mss_req = NULL;
	}
    return NGX_ERROR;
}

static ngx_int_t
ngx_media_worker_mss_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_uint_t lens = sizeof(ngx_worker_mss_ctx_t);
    ngx_worker_mss_ctx_t *worker_ctx = ngx_pcalloc(ctx->pool,lens);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mss_init begin");

    if(NULL == worker_ctx) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mss_init allocate mss ctx fail.");
        return NGX_ERROR;
    }

    ngx_media_worker_unescape_uri(ctx);

    ctx->priv_data_size    = lens;
    ctx->priv_data         = worker_ctx;

    worker_ctx->pool       = ctx->pool;
    worker_ctx->log        = ctx->log;
    worker_ctx->watcher    = watch;
    worker_ctx->wk_ctx     = ctx;

    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));
    ngx_memzero(&worker_ctx->mss_arg, sizeof(ngx_worker_mss_arg_t));
    worker_ctx->mss_req    = NULL;
    ngx_str_null(&worker_ctx->mss_req_msg);
    ngx_str_null(&worker_ctx->mss_resp_msg);

    worker_ctx->run_handle = NULL;
    ngx_memzero(&worker_ctx->worker_stat, sizeof(mk_task_stat_info_t));
    worker_ctx->mk_nparamcount = 0;
    worker_ctx->mk_paramlist   = NULL;

    /* parser the mss args */
    if(NGX_OK != ngx_media_worker_mss_parser_args(ctx)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mss_init,parser the mss args fail.");
        return NGX_ERROR;
    }

    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_INIT;

    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_mss_release(ngx_media_worker_ctx_t* ctx)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_OK;
    }

    if(NULL == ctx->priv_data) {
        return NGX_OK;
    }
    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;
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
ngx_media_worker_mss_start(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mss_start worker:[%V] begin.",&ctx->wokerid);
    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;


    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));

    worker_ctx->timer.handler = ngx_media_worker_mss_timer;
    worker_ctx->timer.log     = ctx->log;
    worker_ctx->timer.data    = worker_ctx;
    worker_ctx->status        = NGX_MEDIA_WOKER_MSS_STATUS_INIT;

    ngx_add_timer(&worker_ctx->timer,1000);/* start mss work after 1 second */

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mss_start worker:[%V] end.",&ctx->wokerid);

    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mss_stop(ngx_media_worker_ctx_t* ctx)
{

    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_stop_task(worker_ctx->run_handle);
    }
    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_END;
    return NGX_OK;
}
static ngx_int_t    
ngx_media_worker_mss_control(ngx_media_worker_ctx_t* ctx,ngx_uint_t type,const char* name,const char* value)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;

    if(NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN != worker_ctx->status ) {
        return NGX_OK;
    }
    if(worker_ctx->run_handle){
        mk_control_task(worker_ctx->run_handle,(uint32_t)type,(char*)name,(char*)value);
    }

    return NGX_OK;
}

/* mss media worker */
ngx_media_worker_t ngx_video_mss_worker = {
    .type             = ngx_media_worker_mss,
    .init_worker      = ngx_media_worker_mss_init,
    .release_worker   = ngx_media_worker_mss_release,
    .start_worker     = ngx_media_worker_mss_start,
    .stop_worker      = ngx_media_worker_mss_stop,
    .control_worker   = ngx_media_worker_mss_control,
};



