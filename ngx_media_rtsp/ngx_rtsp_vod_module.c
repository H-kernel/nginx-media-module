
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Winshining
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_cycle.h>
#include <nginx.h>
#include <ngx_log.h>
#include "ngx_rtsp.h"
#include "ngx_media_include.h"
#include <ngx_http.h>
#include <curl/curl.h>
#include "ngx_schd.h"

#define NGX_VOD_REPORT_SENDRECV_MAX 5
#define NGX_VOD_REPORT_INTERVAL     10
#define NGX_VOD_URI_MAX             1024
#define NGX_VOD_MGX_BODY_MAX        2048

extern ngx_module_t  ngx_rtsp_module;

#define NOTIFY_CURL_USE

#ifndef NOTIFY_CURL_USE
typedef struct ngx_rtsp_vod_notify_s ngx_rtsp_vod_notify_t;

struct ngx_rtsp_vod_notify_s{
    u_char                uri[NGX_VOD_URI_MAX];
    ngx_uint_t            status;
    ngx_rtsp_vod_notify_t* next;                
};
#endif

typedef struct {
    ngx_pool_t               *pool;
    ngx_log_t                *log;
    ngx_str_t                 path;
    ngx_str_t                 on_play;
    ngx_str_t                 on_playdone;
#ifndef NOTIFY_CURL_USE
    ngx_rtsp_vod_notify_t    *list;
    ngx_thread_mutex_t        list_thread_mtx;
    ngx_event_t               time_event;
#endif
} ngx_rtsp_vod_app_conf_t;

/* http report contenxt */
typedef struct {
    ngx_buf_t                      *request;
    ngx_buf_t                      *response;
    ngx_peer_connection_t           peer;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    ngx_uint_t                      sendcount;
    ngx_str_t                       rep_url;
    ngx_url_t                       url;
}ngx_rtsp_vod_report_ctx_t;

static ngx_int_t ngx_rtsp_vod_preconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_rtsp_vod_postconfiguration(ngx_conf_t *cf);
static void     *ngx_rtsp_vod_create_app_conf(ngx_conf_t *cf);
static char     *ngx_rtsp_vod_merge_app_conf(ngx_conf_t *cf, void *parent,void *child);

static ngx_int_t ngx_rtsp_vod_init_process(ngx_cycle_t *cycle);
static int32_t   ngx_rtsp_vod_status_cb(const char *uri, unsigned int enStatus,void* pUserdata);

#ifndef NOTIFY_CURL_USE
static void      ngx_rtsp_vod_report_timer(ngx_event_t *ev);
#endif

static ngx_command_t  ngx_rtsp_vod_commands[] = {
    { ngx_string("play"),
      NGX_RTSP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTSP_APP_CONF_OFFSET,
      offsetof(ngx_rtsp_vod_app_conf_t, path),
      NULL },
    
    { ngx_string("on_play"),
      NGX_RTSP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTSP_APP_CONF_OFFSET,
      offsetof(ngx_rtsp_vod_app_conf_t, on_play),
      NULL },
    
    { ngx_string("on_play_done"),
      NGX_RTSP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTSP_APP_CONF_OFFSET,
      offsetof(ngx_rtsp_vod_app_conf_t, on_playdone),
      NULL },

      ngx_null_command
};


static ngx_rtsp_module_t  ngx_rtsp_vod_module_ctx = {
    ngx_rtsp_vod_preconfiguration,          /* preconfiguration */
    ngx_rtsp_vod_postconfiguration,         /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtsp_vod_create_app_conf,           /* create app configuration */
    ngx_rtsp_vod_merge_app_conf             /* merge app configuration */
};


ngx_module_t  ngx_rtsp_vod_module = {
    NGX_MODULE_V1,
    &ngx_rtsp_vod_module_ctx,              /* module context */
    ngx_rtsp_vod_commands,                 /* module directives */
    NGX_RTSP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_rtsp_vod_init_process,             /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_rtsp_vod_preconfiguration(ngx_conf_t *cf)
{
	return NGX_OK;
}
static ngx_int_t
ngx_rtsp_vod_postconfiguration(ngx_conf_t *cf)
{
	return NGX_OK;
}
static void *
ngx_rtsp_vod_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtsp_vod_app_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_vod_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    ngx_str_null(&conf->path);
    ngx_str_null(&conf->on_play);
    ngx_str_null(&conf->on_playdone);
    conf->log  = NULL;
#ifndef NOTIFY_CURL_USE
    conf->list = NULL;
#endif

    return conf;
}
static char *
ngx_rtsp_vod_merge_app_conf(ngx_conf_t *cf, void *parent,void *child)
{
    ngx_rtsp_vod_app_conf_t *prev = parent;
    ngx_rtsp_vod_app_conf_t *conf = child;

    ngx_conf_merge_str_value(prev->path, conf->path, "/mnt");
    return NGX_CONF_OK;
}
static ngx_int_t 
ngx_rtsp_vod_init_process(ngx_cycle_t *cycle)
{
    ngx_uint_t                   s,a;
    ngx_rtsp_core_srv_conf_t    *cscf, **cscfp;
    ngx_rtsp_core_app_conf_t    *cacf,**cacfp;
    ngx_rtsp_vod_app_conf_t     *vacf;
    ngx_schd_core_conf_t        *sccf;
    ngx_uint_t                   i          = 0;
    ngx_keyval_t                *kv_diskInfo=NULL;
    u_char                       name[TRANS_VPATH_KV_MAX];
    u_char                       value[TRANS_VPATH_KV_MAX];
    u_char                      *last       = NULL;
    ngx_memzero(&name,TRANS_VPATH_KV_MAX);
    ngx_memzero(&value,TRANS_VPATH_KV_MAX);

    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, 
                                    "ngx_media_rtsp_module,the process:[%d] is not 0,no need init rtsp vod.",
                                    ngx_process_slot);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, cycle->log, 0, 
                                     "ngx_media_rtsp_module,the process:[%d] is 0,init rtsp vod.",
                                     ngx_process_slot);

    ngx_rtsp_core_main_conf_t* conf 
            = ngx_rtsp_cycle_get_module_main_conf(cycle,ngx_rtsp_core_module);
    if(NULL == conf) {
        return NGX_OK;
    }

    sccf = (ngx_schd_core_conf_t *)ngx_schd_get_cycle_conf(cycle, ngx_schd_core_module);

    cscfp = conf->servers.elts;
    for (s = 0; s < conf->servers.nelts; s++) {

        cscf = cscfp[s]->ctx->srv_conf[ngx_rtsp_core_module.ctx_index];
        if(NULL == *cscf->handle) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,the handle is not init.");
            continue;
        }
         /* add the shcd path */    
    
        if(NULL != sccf) {
            /* add the disk info to rtsp lib */
            if(sccf->sch_disk) {
                kv_diskInfo = sccf->sch_disk->elts;
                for(i = 0;i < sccf->sch_disk->nelts;i++) {
                    last = ngx_cpymem(name,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
                    *last = '\0';
                    last = ngx_cpymem(value,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
                    *last = '\0';
                    if(0 != as_rtsp_svr_add_vod_path(*cscf->handle,(char*)name,(char*)value)) {
                        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,add vod:[%s] path[%s] fail.",name,value);
                        continue;
                    }
                }
            }
        }
        cacfp = cscf->applications.elts;
        for (a = 0; a < cscf->applications.nelts; a++) {
            cacf = cacfp[a]->app_conf[ngx_rtsp_core_module.ctx_index];
            vacf = cacfp[a]->app_conf[ngx_rtsp_vod_module.ctx_index];
            if((NULL == vacf->path.data)||(0 == vacf->path.len)) {
                ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "ngx_media_rtsp_module,vod module no set vod path.");
                continue;
            }

            vacf->log  = cycle->log;
            vacf->pool = cycle->pool;
#ifndef NOTIFY_CURL_USE
            vacf->list = NULL;
            if (ngx_thread_mutex_create(&vacf->list_thread_mtx, vacf->log) != NGX_OK) {
                return NGX_ERROR;
            }
#endif
            u_char* pszName = ngx_pcalloc(cycle->pool, (cacf->name.len + 1));
            if(NULL == pszName){
                ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,memroy fail.");
                return NGX_ERROR;
            }
            u_char* pszPath = ngx_pcalloc(cycle->pool, (vacf->path.len + 1));
            if(NULL == pszPath){
                ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,memroy fail.");
                return NGX_ERROR;
            }
            u_char* pName = cacf->name.data;
            ngx_uint_t NameLen = cacf->name.len;
            if('/' == pName[0]) {
                pName++;
                NameLen--;
            }
            if('/' == pName[NameLen]) {
                NameLen--;
            }
            u_char* last = ngx_cpymem(pszName,pName,NameLen);
            *last = '\0';

            last = ngx_cpymem(pszPath,vacf->path.data,vacf->path.len);
            *last = '\0';
            
            if(0 != as_rtsp_svr_add_vod_path(*cscf->handle,(char*)pszName,(char*)pszPath)) {
                ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,add vod:[%s] path[%s] fail.",pszName,pszPath);
                return NGX_ERROR;
            }
            ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "ngx_media_rtsp_module,add vod:[%s] path[%s] success.",pszName,pszPath);

            if(((NULL != vacf->on_play.data)&&(0 != vacf->on_play.len))
              ||((NULL != vacf->on_playdone.data)&&(0 != vacf->on_playdone.len))) {
                  as_rtsp_svr_status_callback(*cscf->handle,ngx_rtsp_vod_status_cb,vacf);
            }
#ifndef NOTIFY_CURL_USE
            ngx_memzero(&vacf->time_event, sizeof(ngx_event_t));
            vacf->time_event.handler = ngx_rtsp_vod_report_timer;
            vacf->time_event.log  = cycle->log;
            vacf->time_event.data = vacf;
            ngx_add_timer(&vacf->time_event, 5000);
            ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_media_rtsp_module,add the check timer,timer set:[%d], process:[%d].",vacf->time_event.timer_set,ngx_process_slot);
#endif
        }
    }
    return NGX_OK;
}
#ifndef NOTIFY_CURL_USE
static void
ngx_rtsp_vod_write_dummy_handler(ngx_event_t *ev)
{
    ngx_log_error(NGX_LOG_INFO,ev->log, 0,
                   "rtsp vod report http dummy handler");
}

static void
ngx_rtsp_vod_write_handler(ngx_event_t *wev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_rtsp_vod_report_ctx_t    *ctx;

    c = wev->data;
    ctx  = c->data;

    ngx_log_error(NGX_LOG_INFO, wev->log, 0,
                   "rtsp vod report http write handler");

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
                      "rtsp vod report http server timed out");

        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        return;
    }

    size = ctx->request->last - ctx->request->pos;

    n = ngx_send(c, ctx->request->pos, size);
    ctx->sendcount++;

    if (n == NGX_ERROR) {
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "rtsp vod send to peer fail!");
        return;
    }

    if (n > 0) {
        ctx->request->pos += n;

        if (n == size) {
            wev->handler = ngx_rtsp_vod_write_dummy_handler;

            if (wev->timer_set) {
                ngx_del_timer(wev);
            }

            if (ngx_handle_write_event(wev, 0) != NGX_OK) {
                ngx_close_connection(c);
                ngx_destroy_pool(ctx->pool);
                ngx_log_error(NGX_LOG_DEBUG, wev->log, 0, "close the handle by the write event");
            }

            return;
        }
    }

    if (NGX_VOD_REPORT_SENDRECV_MAX < ctx->sendcount) {
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "video task send to peer try max times!");
        return;
    }

    if (!wev->timer_set) {
        ngx_add_timer(wev, 1000);
    }
}

static void
ngx_rtsp_vod_read_handler(ngx_event_t *rev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_rtsp_vod_report_ctx_t    *ctx;

    c = rev->data;
    ctx  = c->data;

    ngx_log_error(NGX_LOG_INFO, rev->log, 0,
                   "rtsp vod report http read handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_ERR, rev->log, NGX_ETIMEDOUT,
                      "rtsp vod report http server timed out");
        c->timedout = 1;
        if (rev->timer_set) {
            ngx_del_timer(rev);
        }
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        return;
    }

    if (ctx->response == NULL) {
        ctx->response = ngx_create_temp_buf(ctx->pool, 1024);
        if (ctx->response == NULL) {
            ngx_close_connection(c);
            ngx_destroy_pool(ctx->pool);
            ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
            return;
        }
    }

    size = ctx->response->end - ctx->response->last;

    n = ngx_recv(c, ctx->response->pos, size);

    if (n > 0) {
        ctx->response->last += n;
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
        return;
    }

    if (n == NGX_AGAIN) {
        return;
    }
    ngx_close_connection(c);
    ngx_destroy_pool(ctx->pool);
    ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
}

static ngx_buf_t *
ngx_rtsp_vod_report_create_request(ngx_rtsp_vod_report_ctx_t *ctx,const char* rtsp_url)
{
    xmlDocPtr                 doc         = NULL;/* document pointer */
    xmlNodePtr                rtsp        = NULL;
    xmlChar                  *xmlbuff     = NULL;
    int                       buffersize  = 0;
    u_char                   *last        = NULL;

    size_t     len;
    ngx_buf_t  *b;
    u_char buf[128];
    ngx_memzero(&buf, 128);

    /* Creates a new document, a node and set it as a root node*/
    doc = xmlNewDoc(BAD_CAST "1.0");
    rtsp = xmlNewNode(NULL, BAD_CAST "report");
    xmlNewProp(rtsp, BAD_CAST "url", BAD_CAST rtsp_url);
    xmlDocSetRootElement(doc, rtsp);

    xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
    last = ngx_snprintf(buf, 128, "%d", buffersize);
    *last = '\0';


    len = sizeof("POST ") - 1 + ctx->url.uri.len + sizeof(" HTTP/1.1" CRLF) - 1
          + sizeof("Host: ") - 1 + ctx->url.host.len + sizeof(CRLF) - 1
          + sizeof("User-Agent: AllMedia") - 1 + sizeof(CRLF) - 1
          + sizeof("Connection: close") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Type: application/xml") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Length: ") - 1 + ngx_strlen(buf) + sizeof(CRLF) - 1
          + sizeof(CRLF) - 1
          + buffersize;

    b = ngx_create_temp_buf(ctx->pool, len);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, ctx->log, 0, "rtsp vod report create temp buf fail!");
        return NULL;
    }

    b->last = ngx_cpymem(b->last, "POST ", sizeof("POST ") - 1);
    b->last = ngx_copy(b->last, ctx->url.uri.data, ctx->url.uri.len);
    b->last = ngx_cpymem(b->last, " HTTP/1.1" CRLF,
                         sizeof(" HTTP/1.1" CRLF) - 1);

    b->last = ngx_cpymem(b->last, "Host: ", sizeof("Host: ") - 1);
    b->last = ngx_copy(b->last, ctx->url.host.data,
                         ctx->url.host.len);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "User-Agent: AllMedia", sizeof("User-Agent: AllMedia") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Connection: close", sizeof("Connection: close") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Content-Type: application/xml", sizeof("Content-Type: application/xml") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Content-Length: ", sizeof("Content-Length: ") - 1);
    b->last = ngx_copy(b->last, buf,ngx_strlen(buf));
    *b->last++ = CR; *b->last++ = LF;


    /* add "\r\n" at the header end */
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, xmlbuff, buffersize);

    xmlFree(xmlbuff);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return b;
}


static void
ngx_rtsp_vod_parse_report_url(ngx_rtsp_vod_report_ctx_t *ctx,ngx_str_t* url,const char* rtsp_url)
{
    ngx_memzero(&ctx->url, sizeof(ngx_url_t));

    /*reconstruct the report url */
    u_char* end  = url->data + url->len;
    u_char* last = url->data;
    u_char* p    = NULL;
    size_t size  = url->len + 2;
    size_t asize = 0;

    /* find the rtsp url args */
    u_char* args = (u_char *)ngx_strchr(rtsp_url,'?');
    if(NULL != args) {
        args += 1; /*skip the '?' */
        asize = ngx_strlen(args);
        size += asize;
        ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx rtsp vod notify,rtsp args [%s]",args);
    }

    ctx->rep_url.data = ngx_pcalloc(ctx->pool,size);
    if(NULL == ctx->rep_url.data) {
        return;
    }
    ctx->rep_url.len = size;
    p = ngx_cpymem(ctx->rep_url.data,url->data,url->len);
    if(NULL != args) {
        /* check the report url with args */
        last = ngx_strlchr(last, end, '?');
        if(NULL == last) {
            *p = '?';            
        }
        else {
            *p = '&';;
        }
        p++;
        p = ngx_cpymem(p,args,asize);
    }
    *p = '\0';
    ctx->rep_url.len = ngx_strlen(ctx->rep_url.data);;

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "url in rtsp vod \"%V\"",&ctx->rep_url);

    ctx->url.url = ctx->rep_url;
    ctx->url.no_resolve = 1;
    ctx->url.uri_part = 1;

    if (ngx_strncmp(ctx->url.url.data, "http://", 7) == 0) {
        ctx->url.url.len -= 7;
        ctx->url.url.data += 7;
    }

    if (ngx_parse_url(ctx->pool, &ctx->url) != NGX_OK) {
         if (ctx->url.err) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "%s in task \"%V\"", ctx->url.err, &ctx->url.url);
        }
        return ;
    }
    return;
}

static void 
ngx_rtsp_vod_notify(ngx_rtsp_vod_app_conf_t *cf,ngx_str_t* url,const char* rtsp_url)
{
    ngx_int_t                      rc;
    ngx_rtsp_vod_report_ctx_t     *ctx;
    ngx_pool_t                    *pool = NULL;

    ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
                          "ngx rtsp vod notify:[%V] rtsp url [%s]",url,rtsp_url);


    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
    if (pool == NULL) {
        return;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_rtsp_vod_report_ctx_t));
    if (ctx == NULL) {
        ngx_destroy_pool(pool);
        return;
    }

    ctx->pool      = pool;
    ctx->log       = cf->log;
    ctx->sendcount = 0;

    ngx_rtsp_vod_parse_report_url(ctx,url,rtsp_url);

    ctx->request = ngx_rtsp_vod_report_create_request(ctx,rtsp_url);
    if (ctx->request == NULL) {
        ngx_destroy_pool(pool);

        return;
    }

    ctx->peer.sockaddr = ctx->url.addrs->sockaddr;
    ctx->peer.socklen = ctx->url.addrs->socklen;
    ctx->peer.name = &ctx->url.addrs->name;
    ctx->peer.get = ngx_event_get_peer;
    ctx->peer.log = ctx->log;
    ctx->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&ctx->peer);

    if (rc != NGX_OK && rc != NGX_AGAIN ) {
        if (ctx->peer.connection) {
            ngx_close_connection(ctx->peer.connection);
        }
        ngx_log_error(NGX_LOG_EMERG, ctx->log, 0, "rtsp vod connect to peer fail!");
        ngx_destroy_pool(pool);
        return;
    }

    ctx->peer.connection->data = ctx;
    ctx->peer.connection->pool = ctx->pool;

    ctx->peer.connection->read->handler = ngx_rtsp_vod_read_handler;
    //ctx->peer.connection->read->data = ctx;
    ctx->peer.connection->write->handler = ngx_rtsp_vod_write_handler;
    //ctx->peer.connection->write->data = ctx;

    //ngx_msleep(100);


    ngx_add_timer(ctx->peer.connection->read, 5000);
    ngx_add_timer(ctx->peer.connection->write, 1000);

    /* send the request direct */
    if (rc == NGX_OK ) {
        ngx_rtsp_vod_write_handler(ctx->peer.connection->write);
    }
    return;
}
static void 
ngx_rtsp_vod_notify_nginx(ngx_rtsp_vod_app_conf_t *vacf,const char *uri, unsigned int enStatus)
{
    if (ngx_thread_mutex_lock(&vacf->list_thread_mtx, vacf->log) != NGX_OK) {
        return;
    }

    ngx_rtsp_vod_notify_t* notify = ngx_pcalloc(vacf->pool,sizeof(ngx_rtsp_vod_notify_t));
    if(NULL == notify) {
        if (ngx_thread_mutex_unlock(&vacf->list_thread_mtx, vacf->log) != NGX_OK) {
            return;
        }
        return;
    }

    notify->status = enStatus;
    u_char* last = ngx_cpymem(notify->uri,uri,ngx_strlen(uri));
    *last = '\0';

    notify->next = vacf->list;
    vacf->list = notify;

    if (ngx_thread_mutex_unlock(&vacf->list_thread_mtx, vacf->log) != NGX_OK) {
        return;
    }
}

static void
ngx_rtsp_vod_report_timer(ngx_event_t *ev)
{
    ngx_rtsp_vod_app_conf_t *vacf = (ngx_rtsp_vod_app_conf_t*)ev->data;

    ngx_log_error(NGX_LOG_INFO, ev->log, 0,
                          "ngx_media_rtsp_module,ngx rtsp vod report timer start.");

    if (ngx_thread_mutex_unlock(&vacf->list_thread_mtx, ev->log) != NGX_OK) {
        return;
    }

    ngx_rtsp_vod_notify_t* list = vacf->list;
    ngx_rtsp_vod_notify_t* next;

    while(NULL != list) {
        if(AS_RTSP_SERVER_STATUS_DESCRIBE == list->status) {
            if((NULL == vacf->on_play.data)||(0 == vacf->on_play.len)) {
                return;
            }
            /* send on play */
            ngx_rtsp_vod_notify(vacf,&vacf->on_play,(const char*)&list->uri[0]);
        }
        else if(AS_RTSP_SERVER_STATUS_BREAK == list->status) {
            if((NULL == vacf->on_playdone.data)||(0 == vacf->on_playdone.len)) {
                return;
            }
            /* send on play done*/
            ngx_rtsp_vod_notify(vacf,&vacf->on_playdone,(const char*)&list->uri[0]);
        }
        next = list->next;
        ngx_pfree(vacf->pool,list);
        list = next;
    }
    vacf->list = NULL;

    (void)ngx_thread_mutex_unlock(&vacf->list_thread_mtx, ev->log);    

    ngx_add_timer(&vacf->time_event, 5000);

    return;
}
#else
static size_t
ngx_rtsp_vod_notify_curl_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    CURL* req = (CURL*)userdata;
    if(NULL == req) {
        return NGX_ERROR;
    }

    size_t recv_size = size * nmemb;
    /* nothing to do */
    return recv_size;
}
static void
ngx_rtsp_vod_notify_curl_parse_report_url(char* pszReprtoUrl,ngx_uint_t max_size,ngx_str_t* url,const char* rtsp_url)
{
    /*reconstruct the report url */
    u_char* end  = url->data + url->len;
    u_char* last = url->data;
    u_char* p    = NULL;
    size_t size  = url->len + 2;
    size_t asize = 0;

    /* find the rtsp url args */
    u_char* args = (u_char *)ngx_strchr(rtsp_url,'?');
    if(NULL != args) {
        args += 1; /*skip the '?' */
        asize = ngx_strlen(args);
        size += asize;
    }

    if(size >= max_size) {
        return;
    }

    p = ngx_cpymem(pszReprtoUrl,url->data,url->len);
    if(NULL != args) {
        /* check the report url with args */
        last = ngx_strlchr(last, end, '?');
        if(NULL == last) {
            *p = '?';            
        }
        else {
            *p = '&';;
        }
        p++;
        p = ngx_cpymem(p,args,asize);
    }
    *p = '\0';
    return;
}
static void
ngx_rtsp_vod_notify_curl_build_msg(char* pszMsg,ngx_uint_t max_size,const char* rtsp_url)
{
    xmlDocPtr                 doc         = NULL;/* document pointer */
    xmlNodePtr                rtsp        = NULL;
    xmlChar                  *xmlbuff     = NULL;
    int                       buffersize  = 0;
    u_char                   *last        = NULL;

    /* Creates a new document, a node and set it as a root node*/
    doc = xmlNewDoc(BAD_CAST "1.0");
    rtsp = xmlNewNode(NULL, BAD_CAST "report");
    xmlNewProp(rtsp, BAD_CAST "url", BAD_CAST rtsp_url);
    xmlDocSetRootElement(doc, rtsp);

    xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
    ngx_uint_t size = buffersize;
    if(size < max_size) {
        last  = ngx_cpymem(pszMsg, xmlbuff, buffersize);
        *last = '\0';
    }

    xmlFree(xmlbuff);
    xmlFreeDoc(doc);
    xmlCleanupParser();
}
static ngx_int_t 
ngx_rtsp_vod_notify_curl(ngx_rtsp_vod_app_conf_t *vacf,const char *uri, unsigned int enStatus)
{
    CURL                           *req;
    struct curl_slist              *headList;
    ngx_int_t                       ret = 0;
    u_char                          szUrl[NGX_VOD_URI_MAX];
    u_char                          szMsg[NGX_VOD_MGX_BODY_MAX];

    if(AS_RTSP_SERVER_STATUS_DESCRIBE == enStatus) {
        if((NULL == vacf->on_play.data)||(0 == vacf->on_play.len)) {
            return 0;
        }
        ngx_rtsp_vod_notify_curl_parse_report_url((char*)&szUrl[0],NGX_VOD_URI_MAX,&vacf->on_play,uri);
    }
    else if(AS_RTSP_SERVER_STATUS_BREAK == enStatus) {
        if((NULL == vacf->on_playdone.data)||(0 == vacf->on_playdone.len)) {
            return 0;
        }
        ngx_rtsp_vod_notify_curl_parse_report_url((char*)&szUrl[0],NGX_VOD_URI_MAX,&vacf->on_playdone,uri);
    }
    else {
        return 0;
    }
    ngx_rtsp_vod_notify_curl_build_msg((char*)&szMsg[0],NGX_VOD_MGX_BODY_MAX,uri);
    req = curl_easy_init();;
    if(NULL == req) {
        ngx_log_error(NGX_LOG_WARN, vacf->log, 0,
                              "ngx_rtsp_vod_notify_curl, uri:[%s] curl init fail.",
                              uri);
        return 0;
    }
    curl_easy_setopt(req, CURLOPT_USERAGENT, "alltask/1.0");
	curl_easy_setopt(req, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, 5);
	curl_easy_setopt(req, CURLOPT_TIMEOUT, 5);

	headList = curl_slist_append(NULL, "Content-Type:application/xml");
	curl_easy_setopt(req, CURLOPT_HTTPHEADER, headList);

	curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, ngx_rtsp_vod_notify_curl_callback);
	curl_easy_setopt(req, CURLOPT_WRITEDATA, req);

    curl_easy_setopt(req, CURLOPT_URL, szUrl);

    curl_easy_setopt(req, CURLOPT_POSTFIELDS, (char*)szMsg);

    ngx_log_error(NGX_LOG_DEBUG, vacf->log, 0,
                              "ngx_rtsp_vod_notify_curl, uri:[%s] ,request:\n%s.",
                              uri,szMsg);
    ret = 0;
    do
    {
        CURLcode eResult = curl_easy_perform(req);
    	if (CURLE_OK != eResult) {
            ngx_log_error(NGX_LOG_WARN, vacf->log, 0,
                              "ngx_rtsp_vod_notify_curl, uri:[%s] notify:[%s]"
                              "curl perform fail,code:[%d],result:[%s].",
                              uri,szUrl,
                              eResult,curl_easy_strerror(eResult));
    		break;
    	}

        long nResponseCode;
    	curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &nResponseCode);

    	if (NGX_HTTP_OK != nResponseCode) {
            ngx_log_error(NGX_LOG_WARN, vacf->log, 0,
                              "ngx_rtsp_vod_notify_curl, uri:[%s]  notify:[%s] HTTP response is not 200 OK,code:[%d].",
                              uri,szUrl,nResponseCode);
            ret = -1;
    		break;
    	}

    }while(0);

    ngx_log_error(NGX_LOG_DEBUG, vacf->log, 0,
                              "ngx_rtsp_vod_notify_curl, uri:[%s] notify:[%s] end",
                              uri,szUrl);

    if (NULL != headList) {
		curl_slist_free_all(headList);
        headList = NULL;
	}

	if (NULL != req) {
		curl_easy_cleanup(req);
		req = NULL;
	}
    return ret;
}
#endif
static int32_t      
ngx_rtsp_vod_status_cb(const char *uri, unsigned int enStatus,void* pUserdata)
{
    ngx_rtsp_vod_app_conf_t *vacf = (ngx_rtsp_vod_app_conf_t*)pUserdata;
    if(NULL == vacf) {
        return -1;
    }

    ngx_log_error(NGX_LOG_DEBUG, vacf->log, 0,
                          "ngx rtsp vod status callback,uri:[%s] status:[%d] ngx_process_slot:[%d]",
                           uri,enStatus,ngx_process_slot);

#ifndef NOTIFY_CURL_USE
    ngx_rtsp_vod_notify_nginx(vacf,uri,enStatus);
    return 0;
#else
    return ngx_rtsp_vod_notify_curl(vacf,uri,enStatus);
#endif
}

