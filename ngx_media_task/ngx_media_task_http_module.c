/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_task_http_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
            2.2016-04-29: add the video task task
******************************************************************************/


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_cycle.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include <ngx_file.h>
#include <ngx_thread.h>
#include <ngx_thread_pool.h>
#include "ngx_media_include.h"
#include <zmq.h>

#define NGX_MEDIA_TASK_RECV_TIMEOUT    500
#define NGX_MEDIA_TASK_RECV_COUNT      5

typedef struct {
    ngx_str_t               zmq_endpoint;
} ngx_media_task_http_local_conf_t;

typedef struct {
    ngx_http_request_t     *request;
    ngx_event_t             timer;
    ngx_uint_t              count;
    void                   *zmp_ctx;
    void                   *zmp_socket;
} ngx_media_task_http_ctx_t;

static char*     ngx_media_task_http_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*     ngx_media_task_http_create_loc_conf(ngx_conf_t *cf);
static void      ngx_media_task_http_recv_request(ngx_http_request_t *r);
static void      ngx_media_task_http_recv_response(ngx_event_t *ev);


static ngx_command_t  ngx_media_task_http_commands[] = {

        { ngx_string(NGX_VIDEO_TASK),
        NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
        ngx_media_task_http_init,
        0,
        0,
        NULL },
        
        { ngx_string(NGX_VIDEO_ENDPIONT),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_media_task_http_local_conf_t, zmq_endpoint),
        NULL
        },

        ngx_null_command
};


static ngx_http_module_t  ngx_media_task_http_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_media_task_http_create_loc_conf,    /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_task_http_module = {
    NGX_MODULE_V1,
    &ngx_media_task_http_module_ctx,       /* module context */
    ngx_media_task_http_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_media_task_http_zmq_request(ngx_http_request_t *r,const char* req,size_t len)
{
    ngx_media_task_http_local_conf_t  *conf;
    ngx_media_task_http_ctx_t         *ctx;  
    u_char szEndpoint[NGX_TASK_ZMQ_ENDPOINT_LEN];
    u_char *last = NULL;
    zmq_msg_t request;

    ngx_memzero(szEndpoint,NGX_TASK_ZMQ_ENDPOINT_LEN);

    conf = ngx_http_get_module_loc_conf(r, ngx_media_task_http_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video task ,get media task http module fail.");
        return NGX_ERROR;
    }

    ctx = (ngx_media_task_http_ctx_t*)ngx_pcalloc(r->pool,sizeof(ngx_media_task_http_ctx_t));
    if(NULL == ctx) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video task ,alloc http task ctx memeory fail.");
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,zmq send msg:\n%s",req);

    do {
        (void)zmq_msg_init_size (&request, len);

        ctx->zmp_ctx = zmq_ctx_new();
        if(NULL == ctx->zmp_ctx) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "video task ,create zmq ctx fail.");
            break;
        }
        ctx->zmp_socket = zmq_socket(ctx->zmp_ctx, ZMQ_REQ);
        if(NULL == ctx->zmp_socket) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "video task ,create zmq socket fail.");
            break;
        }

        if(NULL == conf->zmq_endpoint.data || 0 == conf->zmq_endpoint.len) {
            last = ngx_cpystrn(szEndpoint,(u_char*)NGX_TASK_ZMQ_ENDPIONT,NGX_TASK_ZMQ_ENDPOINT_LEN);
        }
        else {
            last = ngx_cpymem(szEndpoint,conf->zmq_endpoint.data,conf->zmq_endpoint.len);
        }
        *last = '\0';

        if(0 != zmq_connect (ctx->zmp_socket, (char*)&szEndpoint[0])) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "video task ,connect zmq endpoint fail.");
            break;
        }
        /* Fill in message content with 'AAAAAA' */
        memcpy (zmq_msg_data (&request), req, len);
        /* Send the message to the socket */
        int zrc = zmq_sendmsg (ctx->zmp_socket, &request, 0); 
        if (zrc != (int)len) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "video task ,zmq send fail:\n%s.",zmq_msg_data(&request));
            break;
        }

        zmq_msg_close(&request);  
        
        /* add the recv timer */
        ctx->request       = r;
        ctx->count         = 0;
        ngx_memzero(&ctx->timer, sizeof(ngx_event_t));
        ctx->timer.handler = ngx_media_task_http_recv_response;
        ctx->timer.log     = r->connection->log;
        ctx->timer.data    = ctx;

        ngx_add_timer(&ctx->timer, NGX_MEDIA_TASK_RECV_TIMEOUT);

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,zmq send success,wait for response.");
               
        return NGX_OK;

    }while(0);
    
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "video task ,request fail.");
    if(NULL != ctx->zmp_socket) {
        zmq_close(ctx->zmp_socket);
        ctx->zmp_socket = NULL;
    }
    if(NULL != ctx->zmp_ctx) {
        zmq_ctx_destroy(ctx->zmp_ctx);
        ctx->zmp_ctx = NULL;
    }
    return NGX_ERROR;
}

static void
ngx_media_task_http_recv_request(ngx_http_request_t *r)
{
    u_char       *p;
    u_char       *req;
    size_t        len,recv;
    ngx_buf_t    *buf;
    ngx_chain_t  *cl;

    if (r->request_body == NULL
        || r->request_body->bufs == NULL
        || r->request_body->temp_file)
    {
        return ;
    }

    /* If there's an empty body, it's a bad request */
    if (r->headers_in.content_length_n <= 0) {
        r->headers_out.status = NGX_HTTP_BAD_REQUEST;
        r->header_only = 1;
        r->keepalive   = 0;
        r->header_only = 1;
        ngx_http_finalize_request(r, ngx_http_send_header(r));
    }

    /*
    cl = r->request_body->bufs;
    buf = cl->buf;
    
    len = buf->last - buf->pos;
    cl = cl->next;

    for ( ; cl; cl = cl->next) {
        buf = cl->buf;
        len += buf->last - buf->pos;
    }
    */

    len = r->headers_in.content_length_n + 1;

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video task ,alloc the buf fail.");
        return ;
    }
    req = p;
    recv = 0;

    cl = r->request_body->bufs;
    for ( ; cl; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
        recv += (buf->last - buf->pos);
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,length:[%d] recv:[%d].",len,recv);

    *p = '\0';

    if(NGX_OK == ngx_media_task_http_zmq_request(r,(const char * )req,len)) {
        /* send response by the timer */
        return;
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video task ,send zmq request fail.");
    r->headers_out.status = NGX_HTTP_INTERNAL_SERVER_ERROR;
    r->header_only = 1;
    r->keepalive   = 0;
    r->header_only = 1;
    ngx_http_finalize_request(r, ngx_http_send_header(r));
    return ;
}

static void      
ngx_media_task_http_recv_response(ngx_event_t *ev)
{
    zmq_msg_t                    response;
    ngx_chain_t                  out;
    ngx_media_task_http_ctx_t   *ctx;
    ngx_http_request_t          *r;

    ctx = (ngx_media_task_http_ctx_t*)ev->data;
    r   = ctx->request;

    zmq_msg_init(&response);
    ctx->count++;
    out.buf = NULL;
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,recv timerc,count:%d.",ctx->count);

    int zrc = zmq_msg_recv(&response,ctx->zmp_socket, ZMQ_DONTWAIT); 
    if(zrc == -1 ) {
        if(zmq_errno() == EAGAIN && NGX_MEDIA_TASK_RECV_COUNT >ctx->count) {
            zmq_msg_close(&response); 
            ngx_add_timer(&ctx->timer, NGX_MEDIA_TASK_RECV_TIMEOUT);
            ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,try again later,count:%d.",ctx->count);
            return;/* try again */
        }
        else {
            /* send the error reponse */
            r->headers_out.status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video task ,recv error,count:%d.",ctx->count);
        }
    }
    else {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "video task ,zmq response:\n%s.",zmq_msg_data(&response));

        ngx_uint_t size = zmq_msg_size(&response);
        u_char* data = ngx_pcalloc(r->pool, size);
        ngx_memcpy(data,zmq_msg_data(&response), size);

        ngx_buf_t* b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (b != NULL) {
            out.buf    = b;
            out.next   = NULL;
            b->pos      = data; /* first position in memory of the data */
            b->last     = data + size; /* last position */
            b->memory   = 1; /* content is in read-only memory */
            b->last_buf = 1; /* there will be no more buffers in the request */
        }
        r->headers_out.status = NGX_HTTP_OK;
        zmq_msg_close(&response);        
    }

    if(NULL != ctx->zmp_socket) {
        zmq_close(ctx->zmp_socket);
        ctx->zmp_socket = NULL;
    }
    if(NULL != ctx->zmp_ctx) {
        zmq_ctx_destroy(ctx->zmp_ctx);
        ctx->zmp_ctx = NULL;
    }

    r->header_only = 1;
    r->keepalive   = 0;

    if(NULL == out.buf) {
        r->header_only = 1;
        ngx_http_finalize_request(r, ngx_http_send_header(r));
    }
    else {
        r->header_only = 0;
        ngx_int_t rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            ngx_http_finalize_request(r,rc);
            return;
        }

        ngx_http_finalize_request(r,ngx_http_output_filter(r, &out));
    }
        
}

static ngx_int_t
ngx_media_task_http_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    /* deal the http request with xml content */
    r->request_body_in_single_buf = 1;
    rc = ngx_http_read_client_request_body(r,ngx_media_task_http_recv_request);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    /* return the NGX_DONE and send response by the ngx_media_task_http_recv_request */
    return NGX_DONE;
}


static char*
ngx_media_task_http_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_task_http_handler;
    return NGX_CONF_OK;
}

static void*     
ngx_media_task_http_create_loc_conf(ngx_conf_t *cf)
{
    ngx_media_task_http_local_conf_t  *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_task_http_local_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_str_null(&conf->zmq_endpoint);
    return conf;
}



