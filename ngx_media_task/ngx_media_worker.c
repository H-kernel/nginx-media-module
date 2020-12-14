#include "ngx_media_worker.h"
#include "libMediaKenerl.h"
#include "mk_def.h"


#define DOUBLE_PIONT 100.00
/** head of registered worker linked list */
static ngx_media_worker_t *first_worker =  NULL;

void
ngx_media_register_all_worker()
{
    /*init the */
    extern ngx_media_worker_t ngx_video_mediakernel_worker;
    first_worker = &ngx_video_mediakernel_worker;
    extern ngx_media_worker_t ngx_video_transfer_worker;
    ngx_video_mediakernel_worker.next = &ngx_video_transfer_worker;
    extern ngx_media_worker_t ngx_video_mss_worker;
    ngx_video_transfer_worker.next = &ngx_video_mss_worker;

    ngx_video_mss_worker.next = NULL;
    return;
}


ngx_int_t
ngx_media_worker_init_worker_ctx(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_media_worker_t* worker = NULL;

    worker = ngx_http_find_worker(ctx->type);

    if(NULL == worker) {
        return NGX_ERROR;
    }
    ctx->worker = worker;
    return ctx->worker->init_worker(ctx,watch);
}


static ngx_media_worker_t*
ngx_media_worker_next(const ngx_media_worker_t* woker)
{
    if (woker)
        return woker->next;
    else
        return first_worker;
}

ngx_media_worker_t*
ngx_http_find_worker(ngx_uint_t type)
{
    ngx_media_worker_t *worker = ngx_media_worker_next(NULL);
    while (NULL != worker) {
        if (type == worker->type) {
            return worker;
        }
        worker = ngx_media_worker_next(worker);
    }
    return NULL;
}

ngx_int_t
ngx_media_worker_arg_value_int(u_char* value,ngx_int_t* out)
{
    ngx_int_t i = ngx_atoi(value, ngx_strlen(value));
    if(NGX_ERROR == i) {
        return NGX_ERROR;
    }
    *out = i;
    return NGX_OK;
}

ngx_int_t
ngx_media_worker_arg_value_uint(u_char* value,ngx_uint_t* out)
{
    ngx_int_t i = ngx_atoi(value, ngx_strlen(value));
    if(NGX_ERROR == i) {
        return NGX_ERROR;
    }
    *out = (ngx_uint_t)i;
    return NGX_OK;
}

ngx_int_t
ngx_media_worker_arg_value_double(u_char* value,double* out)
{
    ngx_int_t i = ngx_atofp(value,4,2);
    if(NGX_ERROR == i) {
        return NGX_ERROR;
    }
    *out = (double)(i/DOUBLE_PIONT);
    return NGX_OK;
}

ngx_int_t
ngx_media_worker_arg_value_str(ngx_pool_t *pool,u_char* value,ngx_str_t* out)
{
    if(NULL == pool) {
        return NGX_ERROR;
    }
    size_t size = ngx_strlen(value);
    out->data = ngx_pcalloc(pool,size+1);
    out->len  = size;
    u_char* last = ngx_copy(out->data, value, size);
    *last = '\0';
    return NGX_OK;
}

ngx_int_t
ngx_media_worker_get_file_name(ngx_pool_t *pool,ngx_str_t* full_path,ngx_str_t* name)
{
    ngx_int_t index = full_path->len -1;
    u_char data     = 0;

    for(;index >= 0;index--) {
        data = full_path->data[index];
        if('/' == data) {
            break;
        }
    }

    size_t size = full_path->len - index;
    name->data = ngx_pcalloc(pool,size);
    name->len  = size;
    u_char* last = ngx_copy(name->data, &full_path->data[index+1], size -1);
    *last = '\0';

    return NGX_OK;
}

ngx_int_t
ngx_media_worker_get_file_path(ngx_pool_t *pool,ngx_str_t* full_path,ngx_str_t* path)
{
    ngx_int_t index = full_path->len -1;
    u_char data     = 0;

    for(;index >= 0;index--) {
        data = full_path->data[index];
        if('/' == data) {
            break;
        }
    }

    size_t size = index + 1;
    path->data = ngx_pcalloc(pool,size+1);
    path->len  = size;
    u_char* last = ngx_copy(path->data, &full_path->data, size);
    *last = '\0';

    return NGX_OK;

}

ngx_int_t
ngx_media_worker_parse_url(ngx_pool_t *pool,ngx_str_t* url,ngx_str_t* base_url,ngx_str_t* auth)
{
    u_char    *end    = NULL;
    u_char    *prefix = NULL;

    end = (u_char*)ngx_strchr(url->data, '@');
    if (NULL == end)
    {
        return NGX_ERROR;
    }
    prefix = (u_char*)ngx_strstr(url->data,"//");
    if (NULL == prefix)
    {
        return NGX_ERROR;
    }
    prefix += 2;

    auth->len = end - prefix;
    size_t size = auth->len + 1;
    auth->data = ngx_pcalloc(pool,size);
    u_char* last = ngx_copy(auth->data, prefix, auth->len);
    *last = '\0';

    /* combine the base url */
    size_t presize  = prefix - url->data;
    size_t bodysize = url->len - presize - auth->len;

    size = presize + bodysize + 1;
    base_url->data = ngx_pcalloc(pool,size);
    base_url->len  = size;

    /* copy the prefix */
    last = ngx_copy(base_url->data, url->data, presize);

    /* copy the url body */
    end += 1;
    last = ngx_copy(last, end, bodysize);
    *last = '\0';

    return NGX_OK;
}


void
ngx_media_worker_unescape_uri(ngx_media_worker_ctx_t* ctx)
{
    ngx_int_t i        = 0;
    u_char   *arg      = NULL;
    u_char   *value    = NULL;
    u_char   *unescape = NULL;

    for(i = 0;i < ctx->nparamcount;i++) {

        arg = ctx->paramlist[i];
        if((0 != ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_MK_SRC,ngx_strlen(NGX_HTTP_VIDEO_ARG_MK_SRC)))
            &&(0 != ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_MK_DST,ngx_strlen(NGX_HTTP_VIDEO_ARG_MK_DST)))) {
            continue;
        }
        if((i + 1) >= ctx->nparamcount) {
            continue;
        }

        value = ctx->paramlist[i+1];

        ngx_uint_t lens  = ngx_strlen(value);
        unescape = ngx_pcalloc(ctx->pool,lens+1);
        u_char* pszDst = unescape;
        ngx_unescape_uri(&pszDst,&value, lens, 0);
        pszDst = '\0';

        if((4 < ngx_strlen(unescape))
        &&((0 == ngx_strncmp(unescape,"http://",7))
           ||(0 == ngx_strncmp(unescape,"https://",8))
           ||(0 == ngx_strncmp(unescape,"rtmp://",7))
           ||(0 == ngx_strncmp(unescape,"rtsp://",7))) ){
                ctx->paramlist[i+1] = unescape;
                ngx_pfree(ctx->pool,value);
           }
    }
}


