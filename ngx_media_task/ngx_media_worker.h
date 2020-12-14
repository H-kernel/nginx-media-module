/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_HTTP_VIDEO_WORKER_H_INCLUDED_
#define _NGX_HTTP_VIDEO_WORKER_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
#include "ngx_media_include.h"



#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif


#define NGX_HTTP_PATH_LEN    256

#define NGX_HTTP_WORKER_TRIGGER_MAX 8

#define NGX_HTTP_VIDEO_ARG_MK_SRC    "-src"
#define NGX_HTTP_VIDEO_ARG_MK_DST    "-dst"

enum ngx_http_worker {
    ngx_media_worker_mediakernel     = 0,
    ngx_media_worker_tranfer         = 1,
    ngx_media_worker_mss             = 2,
    ngx_media_worker_invalid
};

enum ngx_media_worker_status {
    ngx_media_worker_status_init     = 0,
    ngx_media_worker_status_start    = 1,
    ngx_media_worker_status_running  = 2,
    ngx_media_worker_status_break    = 3,
    ngx_media_worker_status_end
};

typedef struct {
    ngx_str_t                       wokerid;
    ngx_str_t                       taskid;
    ngx_int_t                       delay;
}ngx_media_worker_trigger_ctx;

typedef struct ngx_media_worker_s ngx_media_worker_t;

typedef struct {
    ngx_str_t                       wokerid;
    ngx_str_t                       taskid;
    ngx_uint_t                      type;
    ngx_uint_t                      master;
    time_t                          starttime;
    time_t                          updatetime;
    ngx_int_t                       status;
    ngx_int_t                       error_code;
    ngx_list_t                     *arglist;
    ngx_int_t                       nparamcount;
    u_char**                        paramlist;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    ngx_resolver_t                 *resolver;
    ngx_msec_t                      resolver_timeout;
    ngx_thread_mutex_t              work_mtx;
    ngx_media_worker_t             *worker;
    ngx_uint_t                      priv_data_size;/* Size of private data .*/
    void                           *priv_data;     /* private data point.*/
    ngx_list_t                     *triggerStart;  /* trigger list(ngx_media_worker_trigger_ctx) after curren worker the starting */
    ngx_list_t                     *triggerEnd;    /* trigger list(ngx_media_worker_trigger_ctx) after curren worker the ending */
} ngx_media_worker_ctx_t;

typedef void (*WK_WATCH)(ngx_uint_t status,ngx_int_t err_code,ngx_media_worker_ctx_t* ctx);

struct ngx_media_worker_s{
    ngx_uint_t                      type;
    ngx_media_worker_t             *next;
    ngx_int_t                       (*init_worker)(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);
    ngx_int_t                       (*release_worker)(ngx_media_worker_ctx_t* ctx);
    ngx_int_t                       (*start_worker)(ngx_media_worker_ctx_t* ctx);
    ngx_int_t                       (*stop_worker)(ngx_media_worker_ctx_t* ctx);
    ngx_int_t                       (*control_worker)(ngx_media_worker_ctx_t* ctx,ngx_uint_t type,const char* name,const char* value);
};


void                     ngx_media_register_all_worker();

ngx_media_worker_t* ngx_http_find_worker(ngx_uint_t type);


ngx_int_t                ngx_media_worker_init_worker_ctx(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);

ngx_int_t                ngx_media_worker_arg_value_int(u_char* value,ngx_int_t* out);
ngx_int_t                ngx_media_worker_arg_value_uint(u_char* value,ngx_uint_t* out);
ngx_int_t                ngx_media_worker_arg_value_double(u_char* value,double* out);
ngx_int_t                ngx_media_worker_arg_value_str(ngx_pool_t *pool,u_char* value,ngx_str_t* out);

ngx_int_t                ngx_media_worker_get_file_name(ngx_pool_t *pool,ngx_str_t* full_path,ngx_str_t* name);
ngx_int_t                ngx_media_worker_get_file_path(ngx_pool_t *pool,ngx_str_t* full_path,ngx_str_t* path);
ngx_int_t                ngx_media_worker_parse_url(ngx_pool_t *pool,ngx_str_t* url,ngx_str_t* base_url,ngx_str_t* auth);

void                     ngx_media_worker_unescape_uri(ngx_media_worker_ctx_t* ctx);









#endif /* _NGX_HTTP_VIDEO_WORKER_H_INCLUDED_ */

