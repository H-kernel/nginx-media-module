/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_schd_http_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
            2.2016-04-29: add file manager.
            3.2017-07-20: add disk manager
******************************************************************************/


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"
#include "ngx_media_sys_stat.h"
#include "ngx_media_common.h"
#include <zookeeper/zookeeper.h>
#include <common/as_json.h>
#include "ngx_schd_core_module.h"



typedef struct {
    ngx_uint_t        type;
} ngx_schd_loc_conf_t;


static char*     ngx_schd_http_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*     ngx_schd_http_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_schd_http_system_json_stat(ngx_http_request_t *r,ngx_chain_t* out);
static ngx_int_t ngx_schd_http_system_xml_stat(ngx_http_request_t *r,ngx_chain_t* out);


static ngx_conf_bitmask_t  ngx_schd_http_type_mask[] = {
    { ngx_string("xml"),          1 },
    { ngx_string("json"),         2 },
    { ngx_null_string,            0 }
};

static ngx_command_t  ngx_schd_http_commands[] = {

    { ngx_string("schd_type"),
      NGX_HTTP_LOC_CONF |NGX_CONF_TAKE1,
      ngx_schd_http_init,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_schd_loc_conf_t, type),
      ngx_schd_http_type_mask },

      ngx_null_command
};


static ngx_http_module_t  ngx_schd_http_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_schd_http_create_loc_conf,          /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_schd_http_module = {
    NGX_MODULE_V1,
    &ngx_schd_http_module_ctx,               /* module context */
    ngx_schd_http_commands,                   /* module directives */
    NGX_HTTP_MODULE,                          /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    NULL,                                     /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    NULL,                                     /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_schd_http_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    int ret;
    ngx_chain_t  out;

    ngx_schd_loc_conf_t* conf = NULL;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "ngx schd http handle request.");

    
    conf = ngx_http_get_module_loc_conf(r, ngx_schd_http_module);
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx schd http manage request method is invalid.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx schd http manage request uri is invalid.");
        return NGX_DECLINED;
    }

    /* discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }
    
    out.buf = NULL;
    if(1 == conf->type) {
        /* xml */
        ret = ngx_schd_http_system_xml_stat(r,&out);
    }
    else if(2 == conf->type) {
        ret = ngx_schd_http_system_json_stat(r,&out);
    }
    else {
         ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
         return NGX_DONE;
    }
    
    if(NGX_OK != ret){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx schd http,deal request fail.");
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_DONE;
    }
    r->headers_out.status = NGX_HTTP_OK;
    r->keepalive   = 0;

    if(NULL == out.buf) {
        r->header_only = 1;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, NGX_DONE);
        return NGX_DONE;
    }
    else {
        r->header_only = 0;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
        return NGX_DONE;
    }
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return NGX_DONE;
}

static void *
ngx_schd_http_create_loc_conf(ngx_conf_t *cf)
{
    ngx_schd_loc_conf_t* conf = NULL;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_schd_loc_conf_t));
    if (conf == NULL)
    {
        return NULL;
    }

    conf->type = 0;
    return conf;
}

static char*
ngx_schd_http_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    if(NGX_CONF_OK != ngx_conf_set_bitmask_slot(cf,cmd,conf)) {
        return NGX_CONF_ERROR;
    }
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_schd_http_handler;

    return NGX_CONF_OK;
}

static ngx_int_t 
ngx_schd_http_system_json_stat(ngx_http_request_t *r,ngx_chain_t* out)
{
    ngx_buf_t *b      = NULL;
    cJSON     *root   = NULL;


    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "ngx_schd_http_system_json_stat,begin");

    root = cJSON_CreateObject();
    if(root == NULL)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_schd_http_system_json_stat,the json root is null");
        return NGX_ERROR;
    }
    
    if(NGX_OK != ngx_schd_zk_get_system_info_json(root)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_schd_http_system_json_stat,get json info fail.");
        cJSON_Delete(root);
        return NGX_ERROR;
    }

    /* 3.build the json infomation and update the zookeeper node */

    u_char* json_buf = (u_char*)cJSON_PrintUnformatted(root);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "ngx_schd_zk_get_system_info_json:value:[%s].",json_buf);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        free(json_buf);
        cJSON_Delete(root);
        return NGX_ERROR;
    }

    ngx_uint_t lens = ngx_strlen(json_buf);

    u_char* pbuff = ngx_pcalloc(r->pool, lens+1);
    if(pbuff == NULL) {
        free(json_buf);
        cJSON_Delete(root);
        return NGX_ERROR;
    }

    u_char* last = ngx_copy(pbuff,json_buf,lens);
    *last = '\0';

    free(json_buf);
    cJSON_Delete(root);

    r->headers_out.content_type.len = sizeof("application/json") - 1;
    r->headers_out.content_type.data = (u_char *) "application/json";
    r->headers_out.content_length_n = lens;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + lens;

    b->memory = 1;
    b->last_buf = 1;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,"ngx_schd_http_system_xml_stat end");

    return NGX_OK;
}
static ngx_int_t
ngx_schd_http_system_xml_stat(ngx_http_request_t *r,ngx_chain_t* out)
{
    xmlDocPtr  root    = NULL;       /* document pointer */
    u_char*  pbuff         = NULL;
    u_char* last           = NULL;
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,"ngx_schd_http_system_xml_stat begin");

    /* Creates a new document, a node and set it as a root node*/
    root = xmlNewDoc(BAD_CAST "1.0");

    if(NGX_OK != ngx_schd_zk_get_system_info_xml(root)) {
        return NGX_ERROR;
    }

    xmlDocDumpFormatMemory(root, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(root);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(root);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(root);

    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char *) "application/xml";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,"ngx_schd_http_system_xml_stat end");

    return NGX_OK;
}


