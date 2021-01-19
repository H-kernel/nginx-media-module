/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_live_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
            2.2019-11-23: add the on_xxxx notify
******************************************************************************/


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_cycle.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include <ngx_thread.h>
#include <ngx_thread_pool.h>
#include <ngx_md5.h>
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"
#include "ngx_shmap.h"
#include "ngx_http_client.h"

static ngx_str_t    shm_name = ngx_string("shm_media_live_session");


/************************ hls session control *******************************************
 *  1. request:/streamname.m3u8/.mpd--->create session
 *             ---> send on_play ---->rewrite /streamname.m3u8/.mpd?token=sessionID
 *  2. request:/streamname.m3u8/.mpd?token=sessionID ---> m3u8/.mpd playlist
 *  3. timer check session: not active session release and send on_play_done
 *
 ***************************************************************************************/

#define MEDIA_LIVE_M3U8      ".m3u8"
#define MEDIA_LIVE_MPD       ".mpd"
#define MEDIA_LIVE_TS        ".ts"
#define MEDIA_LIVE_TOKEN     "token"
#define MEDIA_LIVE_TOKEN_LEN 5
#define MEDIA_LIVE_TOKEN_MAX 128

#define MEDIA_LIVE_SESSION_MAX   4096
#define MEDIA_LIVE_URI_MAX       512
#define MEDIA_LIVE_ARG_MAX       512

typedef enum {
    ngx_media_live_session_start  = 0,
    ngx_media_live_session_run    = 1,
    ngx_media_live_session_stop   = 2
} ngx_media_live_session_status_t;

typedef struct {
    u_char                      uri[MEDIA_LIVE_URI_MAX];
    u_char                      args[MEDIA_LIVE_ARG_MAX];
    u_char                      token[MEDIA_LIVE_TOKEN_MAX];
    time_t                      start;
    time_t                      last;
    ngx_uint_t                  status;
} ngx_media_live_session_t;

typedef struct {
    ngx_str_t                       on_play;
    ngx_str_t                       on_playing;
    ngx_str_t                       on_play_done;
    ngx_msec_t                      timeout;
    ngx_shm_zone_t*                 shm_session;
    ngx_event_t                     timer;
    ngx_log_t                      *log;
} ngx_media_live_loc_conf_t;

typedef struct {
    ngx_str_t                       url;
    ngx_http_request_t             *r;
    u_char                          token[MEDIA_LIVE_TOKEN_MAX];
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
}ngx_media_live_report_ctx_t;

static ngx_int_t ngx_media_live_init_process(ngx_cycle_t *cycle);
static void      ngx_media_live_exit_process(ngx_cycle_t *cycle);
static char*     ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*     ngx_media_live_create_loc_conf(ngx_conf_t *cf);
static void      ngx_media_live_check_session(ngx_event_t *ev);
static ngx_int_t ngx_media_live_report_session_status(ngx_media_live_session_t* s,
                                                      ngx_media_live_loc_conf_t* conf,
                                                      ngx_http_request_t *r);


static ngx_command_t  ngx_media_live_commands[] = {

    { ngx_string(NGX_MEDIA_LIVE),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_media_live_init,
      0,
      0,
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_ONPLAY),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, on_play),
      NULL },
    
    { ngx_string(NGX_MEDIA_LIVE_ONPLAYING),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, on_playing),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_ONPLAY_DONE),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, on_play_done),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_PLAY_TIMEOUT),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t,timeout),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_live_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_media_live_create_loc_conf,         /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_live_module = {
    NGX_MODULE_V1,
    &ngx_media_live_module_ctx,             /* module context */
    ngx_media_live_commands,                /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_media_live_init_process,            /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    ngx_media_live_exit_process,            /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

static void
ngx_media_live_token(u_char *dst, size_t dst_len, u_char *src,size_t src_len)
{
    u_char     result[16], *p;
    ngx_md5_t  md5;

    ngx_md5_init(&md5);
    ngx_md5_update(&md5, src, src_len);
    ngx_md5_final(result, &md5);

    p = ngx_hex_dump(dst, result, ngx_min((dst_len - 1) / 2, 16));
    *p = '\0';
}

static ngx_int_t
ngx_media_live_first_req(ngx_http_request_t *r)
{
    ngx_media_live_loc_conf_t     *conf;
    ngx_media_live_session_t       s;
    u_char                        *last;
    ngx_str_t                      token;
    ngx_str_t                      data;

    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if(r->uri.len >= MEDIA_LIVE_URI_MAX) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the live media request uri is too long.");
        return NGX_HTTP_NOT_ALLOWED;
    }

     /* 1. check the license */
    ngx_uint_t count   = ngx_media_license_hls_current();
    ngx_uint_t licesen = ngx_media_license_hls_channle();

    if (count >= licesen)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                      "http session is full,count:[%d],license:[%d].",count,licesen);
        return NGX_HTTP_NOT_ALLOWED;
    }
    ngx_media_license_hls_inc();
    
    /* 2.add the session info to the share memory */
    s.last   = ngx_time();
    s.start  = ngx_time();
    last     = ngx_cpymem(&s.uri[0],r->uri.data,MEDIA_LIVE_URI_MAX - 1);
    *last    = '\0';
    s.status = ngx_media_live_session_start;

    ngx_media_live_token(&s.token[0],MEDIA_LIVE_TOKEN_MAX,&s.uri[0],ngx_strlen(&s.uri[0]));

    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "create new tocken:[%s].",&s.token[0]);

    
    data.data = (u_char*)&s;
    data.len  = sizeof(ngx_media_live_session_t);

    token.data = (u_char*)&s.token[0];
    token.len  = ngx_strlen(&s.uri[0]);

    if(0 != ngx_shmap_safe_add(conf->shm_session,&token,&data,VT_BINARY,0,0)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"http live media session, add share memory fail.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    /* 3.report the on_play */
    return ngx_media_live_report_session_status(&s,conf,r);
}

static ngx_int_t
ngx_media_live_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    ngx_media_live_loc_conf_t     *conf;
    u_char                        *last;
    size_t                         root;
    ngx_str_t                      reqfile;
    ngx_str_t                      arg;
    ngx_media_live_session_t      *s;
    ngx_str_t                      data;
    uint8_t                        value_type;
    uint32_t                       exptime;
    uint32_t                       user_flags;

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media live request uri is invalid.");
        return NGX_DECLINED;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    last = ngx_http_map_uri_to_path(r, &reqfile, &root, 0);
    if (NULL == last)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the reuquest file path is not exist.");
        return NGX_HTTP_NOT_FOUND;
    }

    /* 1.discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "discard the media live request body fail.");
        return rc;
    }    

    /* 2.check token arg of m3u8 or mpd request*/
    if((4 < reqfile.len)
        &&((NULL != ngx_strstr(reqfile.data,MEDIA_LIVE_M3U8))
        ||(NULL != ngx_strstr(reqfile.data,MEDIA_LIVE_MPD)))) {  
        /* 2.1. check first request without token */
        if (0 == r->args.len) {
            return ngx_media_live_first_req(r);
        }
        if (ngx_http_arg(r, (u_char *) MEDIA_LIVE_TOKEN,MEDIA_LIVE_TOKEN_LEN, &arg) != NGX_OK) {
            return ngx_media_live_first_req(r);
        }
        /* 2.2. update the session info */
        if(0 != ngx_shmap_get(conf->shm_session,&arg,&data,&value_type,&exptime,&user_flags)) {
            return NGX_HTTP_NOT_FOUND;
        }
        s = (ngx_media_live_session_t*)data.data;
        s->last   = ngx_time();
        s->status = ngx_media_live_session_run;
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                    "http  live media session[%s], token:[%s] update.",
                    &s->uri[0],&s->token[0]);
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_media_live_init_process(ngx_cycle_t *cycle)
{
    
    return NGX_OK;
}

static void
ngx_media_live_exit_process(ngx_cycle_t *cycle)
{
    return ;
}


static void *
ngx_media_live_create_loc_conf(ngx_conf_t *cf)
{
    ngx_media_live_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_live_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    ngx_str_null(&conf->on_play);
    ngx_str_null(&conf->on_playing);
    ngx_str_null(&conf->on_play_done);
    ngx_conf_init_msec_value(conf->timeout, NGX_CONF_UNSET_MSEC);
    conf->shm_session = NULL; 

    conf->log = cf->log;  

    return conf;
}



static char*
ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t       *clcf;
    ngx_media_live_loc_conf_t      *mlconf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_live_handler;

    mlconf = ngx_http_conf_get_module_loc_conf(cf, ngx_media_live_module);

    if (NULL == mlconf) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"media live get module conf fail.");
        return NGX_CONF_ERROR;
    }

    /* init the media live session share memory */
    size_t size = (sizeof(ngx_media_live_session_t) + sizeof(ngx_shmap_node_t)) * MEDIA_LIVE_SESSION_MAX;

    mlconf->shm_session = ngx_shmap_init(cf,&shm_name,size,&ngx_media_live_module);
    if(NULL == mlconf->shm_session) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"media live create share session memory fail.");
        return NGX_CONF_ERROR;
    }


    /* start the timer for check session */
    mlconf->timer.handler = ngx_media_live_check_session;
    mlconf->timer.log     = cf->log;
    mlconf->timer.data    = mlconf;

    ngx_add_timer(&mlconf->timer,mlconf->timeout);

    return NGX_CONF_OK;
}

static void 
ngx_media_live_foreach_session(ngx_shmap_node_t* node, void* extarg)
{
    ngx_media_live_loc_conf_t    *conf
            = (ngx_media_live_loc_conf_t*)extarg;
    ngx_media_live_session_t     *s;
    ngx_str_t                     key;

    s = (ngx_media_live_session_t*)&node->data[node->key_len];


    time_t t = time(NULL);
    ngx_uint_t duration = (t - s->last)*1000;
    ngx_uint_t timeout  = conf->timeout*3;
    if(duration > timeout) {
        s->status = ngx_media_live_session_stop;
    }

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"check session:[%s] status:[%d] current:[%d] last:[%d].",
                            &s->token[0],s->status,t,s->last);
    
    (void)ngx_media_live_report_session_status(s,conf,NULL);

    /* delete the session */
    if(ngx_media_live_session_stop == s->status) {
        key.data = &s->token[0];
        key.len  = ngx_strlen(&s->token[0]);
        (void)ngx_shmap_delete(conf->shm_session,&key);
    }
}

static void
ngx_media_live_check_session(ngx_event_t *ev)
{
    ngx_media_live_loc_conf_t    *conf
            = (ngx_media_live_loc_conf_t*)ev->data;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"check session timer begin.");
    /* check all the session */
    if(0 != ngx_shmap_foreach(conf->shm_session,ngx_media_live_foreach_session,conf)) {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,"check session foreach session fail.");
    }

    /* next timer for check */
    conf->timer.handler = ngx_media_live_check_session;
    conf->timer.log     = conf->log;
    conf->timer.data    = conf;

    ngx_add_timer(&conf->timer,conf->timeout);
}


/*
static void 
ngx_media_live_report_write_handle(void *data, ngx_http_request_t *hcr)
{
    return;
}
*/

static void 
ngx_media_live_report_read_body(void *data, ngx_http_request_t *hcr)
{
    ngx_media_live_report_ctx_t* ctx = (ngx_media_live_report_ctx_t*)data;

    ngx_http_request_t             *r;
    ngx_chain_t                    *cl = NULL;
    ngx_int_t                       rc;

    r = ctx->r;

    ngx_log_error(NGX_LOG_INFO, ctx->log, 0,"http client test recv body");

    do {

        rc = ngx_http_client_read_body(hcr, &cl);

        ngx_log_error(NGX_LOG_INFO, ctx->log, 0,
                "http client test recv body, rc %i %i, %O",
            rc, ngx_errno, ngx_http_client_rbytes(hcr));

        if (rc == 0) {
            break;
        }

        if (rc == NGX_ERROR) {
            break;
        }


        if (rc == NGX_AGAIN) {
            continue;
        }

        if (rc == NGX_DONE) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "all body has been read");
            ngx_http_client_finalize_request(hcr, 0);
        }
    }while(NGX_DONE != rc);

    return;
}
static void 
ngx_media_live_report_read_handle(void *data, ngx_http_request_t *hcr)
{
    ngx_media_live_loc_conf_t      *conf;
    ngx_media_live_report_ctx_t    *ctx = (ngx_media_live_report_ctx_t*)data;
    ngx_http_request_t             *r   = ctx->r;
    ngx_media_live_session_t       *s   = NULL;
    ngx_str_t                       token;
    ngx_str_t                       s_data;
    uint8_t                         value_type;
    uint32_t                        exptime;
    uint32_t                        user_flags;
    ngx_str_t                       location;
    u_char                         *last = NULL;

    ngx_uint_t code = ngx_http_client_status_code(hcr);

    //ngx_http_client_set_read_handler(data, ngx_media_live_report_read_body);
    /* recv the body and discard it */
    ngx_media_live_report_read_body(data,hcr); 

    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return ;
    }

    do {
        token.data = &ctx->token[0];
        token.len  = ngx_strlen(&ctx->token[0]);
        /* get the session info */
        if(0 != ngx_shmap_get(conf->shm_session,&token,&s_data,&value_type,&exptime,&user_flags)) {
            break;
        }
        s = (ngx_media_live_session_t*)s_data.data;
    
        /* send http response */
        if(NULL == r) {
            break;
        }
        if((NGX_HTTP_OK != code)||(NULL == s)) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "response from call back fail code:[%d].",code);
            ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
            break;
        }

        location.len  = ngx_strlen(&s->uri[0]) + 1 + ngx_strlen(&s->args[0]) + MEDIA_LIVE_TOKEN_LEN + 2 + ngx_strlen(&s->token[0]) + 1;
        location.data = ngx_pcalloc(r->pool,location.len);
        last = ngx_snprintf(location.data,location.len,"%s?%s&%s=%s",&s->uri[0],&s->args[0],MEDIA_LIVE_TOKEN,&s->token[0]);
        *last = '\0';

        ngx_http_clear_location(r);
        r->headers_out.location = ngx_list_push(&r->headers_out.headers);
        if (!r->headers_out.location) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            break;
        }

        r->headers_out.status = NGX_HTTP_MOVED_PERMANENTLY;
        r->headers_out.content_length_n = 0;
        r->headers_out.location->hash = 1;
        r->headers_out.location->key.data = (u_char*) "Location";
        r->headers_out.location->key.len = sizeof("Location") - 1;
        r->headers_out.location->value.data = (u_char*)location.data;
        r->headers_out.location->value.len = location.len;
        r->header_only = 1;
        ngx_http_finalize_request(r, ngx_http_send_header(r));
    }while(0);

    ngx_destroy_pool(ctx->pool);
    return;
}

static void
ngx_media_live_format_report_session(ngx_media_live_report_ctx_t *ctx,ngx_media_live_session_t* s,ngx_str_t* url)
{
    ngx_uint_t len = url->len + 1; /*args start with '?' */

    if(ngx_media_live_session_start == s->status) {
        len += sizeof("?call=play");
    }
    else if(ngx_media_live_session_run == s->status) {
        len += sizeof("?call=update");
    }
    else if(ngx_media_live_session_stop == s->status) {
        len += sizeof("?call=play_done");
    }

    size_t size = ngx_strlen(s->uri);

    if(0 < size) {
        len += sizeof("&app=") +  sizeof("&name=") + size;
    }

    ctx->url.len = len;
    ctx->url.data = ngx_pcalloc(ctx->pool,len);

    u_char* last = ngx_cpymem(ctx->url.data,url->data,url->len);

    if(ngx_media_live_session_start == s->status) {
        last = ngx_cpymem(last,"?call=play",sizeof("?call=play"));
    }
    else if(ngx_media_live_session_run == s->status) {
        last = ngx_cpymem(last,"?call=update",sizeof("?call=update"));
    }
    else if(ngx_media_live_session_stop == s->status) {
        last = ngx_cpymem(last,"?call=play_done",sizeof("?call=play_done"));
    }

    if(0 < size) {
        u_char* end = s->uri + size;
        u_char* p = ngx_video_strrchr(s->uri,end,'/');
        if(NULL == p) {
            last = ngx_cpymem(last,"&name=",sizeof("&name="));
            last = ngx_cpymem(last,s->uri,size);
        }
        else {
            last = ngx_cpymem(last,"&app=",sizeof("&app="));
            last = ngx_cpymem(last,s->uri,p - s->uri);

            last = ngx_cpymem(last,"&name=",sizeof("&name="));
            last = ngx_cpymem(last,p+1,end - p );
        }
    }
    *last = '\0'; 

    ngx_log_error(NGX_LOG_INFO, ctx->log, 0,"ngx media http live report,report status:[%d] url:[%s]",s->status,&s->uri[0]);

    return;
}

static ngx_int_t
ngx_media_live_report_session_status(ngx_media_live_session_t* s,
                                     ngx_media_live_loc_conf_t* conf,
                                     ngx_http_request_t *r)
{
    ngx_media_live_report_ctx_t   *ctx;
    ngx_pool_t                    *pool = NULL;
    ngx_http_request_t            *hcr  = NULL;
    u_char                        *last = NULL;
    ngx_str_t                     *url  = NULL;

    
    if(ngx_media_live_session_start == s->status) {
        if((NULL == conf->on_play.data)||(0 == conf->on_play.len)) {
            ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"ngx media http live report,onplay is null.");
            return NGX_DECLINED;
        }
        url = &conf->on_play;
    }
    else if(ngx_media_live_session_run == s->status) {
        if((NULL == conf->on_playing.data)||(0 == conf->on_playing.len)) {
            ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"ngx media http live report,onplaying is null.");
            return NGX_DECLINED;
        }
        url = &conf->on_playing;
    }
    else if(ngx_media_live_session_stop == s->status) {
        if((NULL == conf->on_play_done.data)||(0 == conf->on_play_done.len)) {
            ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"ngx media http live report,onplay_done is null.");
            return NGX_DECLINED;
        }
        url = &conf->on_play_done;
    }
    else {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,"ngx media http live report,status is wrong.");
        return NGX_DECLINED;
    }

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0,"ngx media http live report,status:[%d] url:[%V].",s->status,url);


    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, conf->log);
    if (pool == NULL) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,"ngx media http live report,create memory pool fail.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_media_live_report_ctx_t));
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,"ngx media http live report,create ctx memory fail.");
        ngx_destroy_pool(pool);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->pool      = pool;
    ctx->log       = conf->log;

    ctx->url.len   = 0;
    ctx->url.data  = NULL;
    ctx->r         = r;
    last           = ngx_cpymem(&ctx->token[0],s->token,MEDIA_LIVE_TOKEN_MAX - 1);
    *last          = '\0';

    /* parser the url and append the url args */
    ngx_media_live_format_report_session(ctx,s,url);


    hcr = ngx_http_client_create(ctx->log,NGX_HTTP_CLIENT_POST,&ctx->url,NULL,NULL,ctx);
    if(NULL == hcr) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,"ngx media http live report,create http client fail.");
        ngx_destroy_pool(pool);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    

    // add Connection, delete Date, Modify Host, add new header
    //ngx_str_t                   value;

    //value.data = (u_char *) "World";
    //value.len = sizeof("World") - 1;

    //ngx_keyval_t                headers[] = {
    //    { ngx_string("Host"),       ngx_string("www.test.com") },
    //    { ngx_string("Connection"), ngx_string("upgrade") },
    //    { ngx_string("Date"),       ngx_null_string },
    //    { ngx_string("Hello"),      value },
    //    { ngx_null_string,          ngx_null_string } // must end with null str
    //};
    //ngx_http_client_set_headers(r, headers);

    //ngx_http_client_set_write_handler(r, ngx_media_live_report_write_handle);

    ngx_http_client_set_read_handler(hcr, ngx_media_live_report_read_handle);

    ngx_log_error(NGX_LOG_INFO, conf->log, 0,"ngx media http live report before send");

    if(NGX_OK != ngx_http_client_send(hcr)) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    ngx_log_error(NGX_LOG_INFO, conf->log, 0,"ngx media http live report after send");

    return NGX_DONE; /* wait upstream response */
}



