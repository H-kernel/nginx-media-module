/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_filesystem_module.c
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
#include "libMediaKenerl.h"
#include "mk_def.h"
#include "ngx_media_include.h"
#include "ngx_media_sys_stat.h"
#include "ngx_media_common.h"
#include <common/as_json.h>
#include "ngx_media_sys_stat.h"




static char*     ngx_media_filesystem_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_int_t ngx_media_filesystem_disk_stat(ngx_http_request_t *r,ngx_chain_t* out);
static ngx_int_t ngx_media_filesystem_disk_list_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_list_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_list_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_list(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out);
static ngx_int_t ngx_media_filesystem_disk_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_filesystem_disk_delete(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out);
static ngx_int_t ngx_media_filesystem_disk_mkdir(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out);
static ngx_int_t ngx_media_filesystem_disk_req(ngx_http_request_t *r,xmlDocPtr doc,ngx_chain_t* out);


static ngx_int_t ngx_media_filesystem_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out);
static void      ngx_media_filesystem_recv_request(ngx_http_request_t *r);



static ngx_command_t  ngx_media_filesystem_commands[] = {

    { ngx_string(NGX_HTTP_FILE_SYS),
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      ngx_media_filesystem_init,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_filesystem_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_filesystem_module = {
    NGX_MODULE_V1,
    &ngx_media_filesystem_module_ctx,       /* module context */
    ngx_media_filesystem_commands,          /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_media_filesystem_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "ngx http vido handle manage request.");


    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media  request method is invalid.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media  request uri is invalid.");
        return NGX_DECLINED;
    }


    /*  remove the args from the uri */
    if (r->args_start)
    {
        r->uri.len = r->args_start - 1 - r->uri_start;
        r->uri.data[r->uri.len] ='\0';
    }

    /* deal the http request with xml content */
    r->request_body_in_single_buf = 1;
    rc = ngx_http_read_client_request_body(r,ngx_media_filesystem_recv_request);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    /* return the NGX_DONE and send response by the ngx_media_filesystem_recv_request */
    return NGX_DONE;
}

static char*
ngx_media_filesystem_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_filesystem_handler;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_media_filesystem_disk_stat(ngx_http_request_t *r,ngx_chain_t* out)
{
    xmlDocPtr resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL,vpath_node = NULL;/* node pointers */
    u_char*  pbuff        = NULL;
    u_char* last          = NULL;
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    ngx_uint_t i              = 0;
    ngx_uint_t count          = 0;
    ngx_uint_t ulTotalSize    = 0;
    ngx_uint_t ulUsedSize     = 0;
    ngx_uint_t ulFreeSize     = 0;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    u_char    *diskNameArray[TRANS_VPATH_KV_MAX];
    u_char    *diskPath[TRANS_VPATH_KV_MAX];


    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);


    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,"ngx_media_filesystem_disk_stat");
    count = ngx_media_sys_stat_get_all_disk((u_char**)&diskNameArray,(u_char**)&diskPath,TRANS_VPATH_KV_MAX);
 
    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    /*disk stat info*/
    if(0 < count) {
        disk_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK);
        xmlNewProp(disk_node, BAD_CAST COMMON_XML_COMMAND, BAD_CAST SYSTEM_COMMAND_STAT);
        xmlAddChild(root_node, disk_node);
        for(i = 0;i < count;i++) {
            ngx_media_sys_stat_get_diskinfo(diskPath[i],&ulTotalSize,&ulUsedSize);
            vpath_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK_VPATH);

            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_NAME, BAD_CAST diskNameArray[i]);

            last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ulTotalSize);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_SIZE, BAD_CAST cbuf);

            ulFreeSize = ulTotalSize - ulUsedSize;
            last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ulFreeSize);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_FREE, BAD_CAST cbuf);
            xmlAddChild(disk_node, vpath_node);
        }
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char *) "application/xml";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}
static ngx_int_t
ngx_media_filesystem_disk_list_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    xmlNodePtr vpath_node = NULL,file_node = NULL;/* node pointers */
    vpath_node = (xmlNodePtr)ctx->data;

    u_char* begin = path->data;
    u_char* end  = path->data + path->len;
    u_char* last = NULL;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);
    ngx_str_t   file;
    ngx_str_null(&file);

    last = ngx_video_strrchr(begin,end,'/');
    if(NULL != last) {
        file.data = last + 1;
        file.len  = end - last;
    }
    else
    {
        file.data = path->data;
        file.len  = path->len;
    }

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%V",&file);
    *last = '\0';

    file_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK_FILE);

    xmlNewProp(file_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ctx->size);
    *last = '\0';
    xmlNewProp(file_node, BAD_CAST COMMON_XML_SIZE, BAD_CAST cbuf);

    xmlAddChild(vpath_node, file_node);

    return NGX_OK;
}
static ngx_int_t
ngx_media_filesystem_disk_list_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    xmlNodePtr vpath_node = NULL,dir_node = NULL;/* node pointers */
    vpath_node = (xmlNodePtr)ctx->data;

    u_char* begin = path->data;
    u_char* end  = path->data + path->len - 1; /* skip the last '/' */
    u_char* last = NULL;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);
    ngx_str_t   dir;
    ngx_str_null(&dir);

    last = ngx_video_strrchr(begin,end,'/');
    if(NULL != last) {
        dir.data = last;
        dir.len  = end - last + 1;
    }
    else
    {
        dir.data = path->data;
        dir.len  = path->len;
    }

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%V",&dir);
    *last = '\0';

    dir_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK_DIR);

    xmlNewProp(dir_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

    xmlAddChild(vpath_node, dir_node);
    return NGX_OK;
}
static ngx_int_t
ngx_media_filesystem_disk_list_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}


static ngx_int_t
ngx_media_filesystem_disk_list(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlChar*   attr_name  = NULL;
    xmlChar*   attr_dir   = NULL;
    xmlDocPtr  resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL,vpath_node = NULL;/* node pointers */
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    u_char*  pbuff            = NULL;
    ngx_uint_t i              = 0;
    ngx_flag_t find           = 0;
    u_char    *last           = NULL;
    ngx_file_info_t           fi;
    size_t     size           = 0;
    ngx_tree_ctx_t            tree;
    ngx_str_t  path;
    ngx_uint_t count          = 0;
    u_char    *diskNameArray[TRANS_VPATH_KV_MAX];
    u_char    *diskPath[TRANS_VPATH_KV_MAX];

    ngx_str_null(&path);

    count = ngx_media_sys_stat_get_all_disk((u_char**)&diskNameArray,(u_char**)&diskPath,TRANS_VPATH_KV_MAX);

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    disk_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK);
    xmlNewProp(disk_node, BAD_CAST COMMON_XML_COMMAND, BAD_CAST SYSTEM_COMMAND_LIST);
    xmlAddChild(root_node, disk_node);

    /* operate node */
    curNode = Node->children;
    while(NULL != curNode) {

        if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_NODE_DISK_VPATH)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,the node is not vpath");
            curNode = curNode->next;
            continue;
        }

        attr_name = xmlGetProp(curNode,BAD_CAST COMMON_XML_NAME);
        if(NULL == attr_name) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath name not found.");
            curNode = curNode->next;
            continue;
        }
        attr_dir = xmlGetProp(curNode,BAD_CAST SYSTEM_NODE_DISK_DIR);
        if(NULL == attr_dir) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath dir not found.");
            curNode = curNode->next;
            continue;
        }

        /* map the vpath to path of file system */
        if(0 == count) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath is not configure.");
            curNode = curNode->next;
            continue;
        }
        find = 0;
        for(i = 0;i < count;i++) {
            if(0 == ngx_strncmp(attr_name,diskNameArray[i],ngx_strlen(diskNameArray[i]))) {
                find = 1;
                break;
            }
        }

        if(!find) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath is not found.");
            curNode = curNode->next;
            continue;
        }
        path.len = ngx_strlen(diskPath[i]) + ngx_strlen(attr_dir);
        size = path.len;
        path.data = ngx_pcalloc(r->pool,size+1);

        last = ngx_snprintf(path.data,size, "%s%s", diskPath[i],attr_dir);
        *last = '\0';

        /* check all the task xml file,and start the task */
        if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,link the dir:[%V] fail.",&path);
            curNode = curNode->next;
            continue;
        }
        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,the:[%V] is not a dir.",&path);
            curNode = curNode->next;
            continue;
        }

        vpath_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK_VPATH);

        xmlNewProp(vpath_node, BAD_CAST COMMON_XML_NAME, BAD_CAST attr_name);
        xmlNewProp(vpath_node, BAD_CAST SYSTEM_NODE_DISK_DIR, BAD_CAST attr_dir);

        /* walk the list dir */
        tree.init_handler = NULL;
        tree.file_handler = ngx_media_filesystem_disk_list_file;
        tree.pre_tree_handler = ngx_media_filesystem_disk_list_dir;
        tree.post_tree_handler = ngx_media_filesystem_disk_list_noop;
        tree.spec_handler = ngx_media_filesystem_disk_list_noop;
        tree.data = vpath_node;
        tree.alloc = 0;
        tree.log  = r->connection->log;
        if (ngx_walk_tree(&tree, &path) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,walk tree:[%V] fail.",&path);
            xmlFreeNode(vpath_node);
            curNode = curNode->next;
            continue;
        }
        xmlAddChild(disk_node, vpath_node);
        curNode = curNode->next;
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char *) "application/xml";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_media_filesystem_disk_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_filesystem_disk_delete_file,delete file:[%V].",path);

    if (ngx_delete_file(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_filesystem_disk_delete_file,delete file:[%V] fail.",path);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_media_filesystem_disk_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_filesystem_disk_delete_dir,delete dir:[%V].",path);

    if (ngx_delete_dir(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_filesystem_disk_delete_dir,delete dir:[%V] fail.",path);
    }

    return NGX_OK;

}

static ngx_int_t
ngx_media_filesystem_disk_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

static ngx_int_t
ngx_media_filesystem_disk_delete(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlNodePtr childNode  = NULL;
    xmlChar*   attr_name  = NULL;
    xmlChar*   attr_dir   = NULL;
    xmlDocPtr  resp_doc   = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL;/* node pointers */
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    u_char*  pbuff        = NULL;
    ngx_uint_t i              = 0;
    ngx_flag_t find           = 0;
    u_char    *last           = NULL;
    ngx_file_info_t           fi;
    ngx_tree_ctx_t            tree;
    ngx_str_t  path;
    ngx_str_t  sub_path;
    size_t     size           = 0;
    ngx_uint_t count          = 0;
    u_char    *diskNameArray[TRANS_VPATH_KV_MAX];
    u_char    *diskPath[TRANS_VPATH_KV_MAX];

    ngx_str_null(&path);

    count = ngx_media_sys_stat_get_all_disk((u_char**)&diskNameArray,(u_char**)&diskPath,TRANS_VPATH_KV_MAX);



    tree.init_handler = NULL;
    tree.file_handler = ngx_media_filesystem_disk_delete_file;
    tree.pre_tree_handler = ngx_media_filesystem_disk_delete_noop;
    tree.post_tree_handler = ngx_media_filesystem_disk_delete_dir;
    tree.spec_handler = ngx_media_filesystem_disk_delete_file;
    tree.data = NULL;
    tree.alloc = 0;
    tree.log = r->connection->log;

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                               "ngx_media_filesystem_disk_delete,delete file or dir begin.");
    /* operate node */
    curNode = Node->children;
    while(NULL != curNode) {

        if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_NODE_DISK_VPATH)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,the node is not vpath");
            curNode = curNode->next;
            continue;
        }

        attr_name = xmlGetProp(curNode,BAD_CAST COMMON_XML_NAME);
        if(NULL == attr_name) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath name not found.");
            curNode = curNode->next;
            continue;
        }
        attr_dir = xmlGetProp(curNode,BAD_CAST SYSTEM_NODE_DISK_DIR);
        if(NULL == attr_dir) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath dir not found.");
            curNode = curNode->next;
            continue;
        }

        /* map the vpath to path of file system */
        if(0 == count) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath is not configure.");
            curNode = curNode->next;
            continue;
        }
        find = 0;
        for(i = 0;i < count;i++) {
            if(0 == ngx_strncmp(attr_name,diskNameArray[i],ngx_strlen(diskNameArray[i]))) {
                find = 1;
                break;
            }
        }

        if(!find) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_list,vpath is not found.");
            curNode = curNode->next;
            continue;
        }
        path.len = ngx_strlen(diskPath[i]) + ngx_strlen(attr_dir);
        size = path.len;
        path.data = ngx_pcalloc(r->pool, size + 1);

        last = ngx_snprintf(path.data,size, "%s%s", diskPath[i],attr_dir);
        *last = '\0';

        ngx_log_error(NGX_LOG_INFO,  r->connection->log, 0,
                              "ngx_media_filesystem_disk_delete,dir:[%V].",&path);

        /* check all the task xml file,and start the task */
        if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_filesystem_disk_delete,link the dir:[%V] fail.",&path);
            curNode = curNode->next;
            continue;
        }
        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_filesystem_disk_delete,the:[%V] is not a dir.",&path);
            curNode = curNode->next;
            continue;
        }

        childNode = curNode->children;

        if(NULL == childNode) {
             /* walk the delete dir */
            if (ngx_walk_tree(&tree, &path) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "ngx_media_filesystem_disk_delete,walk tree:[%V] fail.",&path);
            }
        }
        else {
            while(NULL != childNode) {
                if (0 == xmlStrcmp(childNode->name, BAD_CAST SYSTEM_NODE_DISK_FILE)) {
                    attr_name = xmlGetProp(childNode,BAD_CAST COMMON_XML_NAME);
                    if(NULL == attr_name) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,file name not found.");
                        childNode = childNode->next;
                        continue;
                    }
                    sub_path.len = path.len + ngx_strlen(attr_name);
                    size = sub_path.len + 1;
                    sub_path.data = ngx_pcalloc(r->pool, size+1);

                    last = ngx_snprintf(sub_path.data,size, "%V/%s", &path,attr_name);
                    *last = '\0';
                    ngx_log_error(NGX_LOG_INFO,  r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,delete file:[%V].",&sub_path);
                    if (ngx_delete_file(sub_path.data) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,delete file:[%V] fail.",&sub_path);
                    }
                }
                else if (0 == xmlStrcmp(childNode->name, BAD_CAST SYSTEM_NODE_DISK_DIR)) {
                    attr_name = xmlGetProp(childNode,BAD_CAST COMMON_XML_NAME);
                    if(NULL == attr_name) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,dir name not found.");
                        childNode = childNode->next;
                        continue;
                    }
                    sub_path.len = path.len + ngx_strlen(attr_name);
                    size = sub_path.len + 1;
                    sub_path.data = ngx_pcalloc(r->pool, size+1);

                    last = ngx_snprintf(sub_path.data,size, "%V/%s", &path,attr_name);
                    *last = '\0';
                    ngx_log_error(NGX_LOG_INFO,  r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,delete sub dir:[%V].",&sub_path);
                    if (ngx_walk_tree(&tree, &sub_path) != NGX_OK) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_filesystem_disk_delete,walk sub tree:[%V] fail.",&sub_path);
                    }
                }

                childNode = childNode->next;
            }
        }

        curNode = curNode->next;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                              "ngx_media_filesystem_disk_delete,delete file or dir end.");
    if(NULL != Node) {
       disk_node = xmlCopyNodeList(Node);
       xmlAddChild(root_node, disk_node);
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char *) "application/xml";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_media_filesystem_disk_mkdir(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlChar*   attr_name  = NULL;
    xmlChar*   attr_dir   = NULL;
    xmlDocPtr  resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL;/* node pointers */
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    u_char*  pbuff        = NULL;
    ngx_uint_t i              = 0;
    ngx_flag_t find           = 0;
    u_char    *last           = NULL;
    ngx_str_t  path;
    size_t     size           = 0;
    ngx_uint_t count          = 0;
    u_char    *diskNameArray[TRANS_VPATH_KV_MAX];
    u_char    *diskPath[TRANS_VPATH_KV_MAX];

    ngx_str_null(&path);

    count = ngx_media_sys_stat_get_all_disk((u_char**)&diskNameArray,(u_char**)&diskPath,TRANS_VPATH_KV_MAX);

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                               "ngx_media_filesystem_disk_mkdir,crreate dir begin.");
    /* operate node */
    curNode = Node->children;
    while(NULL != curNode) {

        if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_NODE_DISK_VPATH)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,the node is not vpath");
            curNode = curNode->next;
            continue;
        }

        attr_name = xmlGetProp(curNode,BAD_CAST COMMON_XML_NAME);
        if(NULL == attr_name) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,vpath name not found.");
            curNode = curNode->next;
            continue;
        }
        attr_dir = xmlGetProp(curNode,BAD_CAST SYSTEM_NODE_DISK_DIR);
        if(NULL == attr_dir) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,vpath dir not found.");
            curNode = curNode->next;
            continue;
        }

        /* map the vpath to path of file system */
        if(0 == count) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,vpath is not configure.");
            curNode = curNode->next;
            continue;
        }
        find = 0;
        for(i = 0;i < count;i++) {
            if(0 == ngx_strncmp(attr_name,diskNameArray[i],ngx_strlen(diskNameArray[i]))) {
                find = 1;
                break;
            }
        }

        if(!find) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,vpath is not found.");
            curNode = curNode->next;
            continue;
        }
        path.len = ngx_strlen(diskPath[i]) + ngx_strlen(attr_dir);
        size = path.len;
        path.data = ngx_pcalloc(r->pool, size + 1);

        last = ngx_snprintf(path.data,size, "%s%s", diskPath[i],attr_dir);
        *last = '\0';

        ngx_log_error(NGX_LOG_INFO,  r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,dir:[%V].",&path);

        /* create the dir */
        //if (ngx_create_dir(path.data,0744) == NGX_FILE_ERROR) {
        //    ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0,
        //                  ngx_create_dir_n " \"%s\" failed", path.data);
        //    break;
        //}
        ngx_int_t eRet = ngx_media_mkdir_full_path(path.data,0666);
        if (eRet) {
            ngx_log_error(NGX_LOG_CRIT, r->connection->log, eRet,
                          ngx_create_dir_n " \"%s\" failed", path.data);
            break;
        }

        curNode = curNode->next;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                              "ngx_media_filesystem_disk_mkdir,make dir end.");
    if(NULL != Node) {
       disk_node = xmlCopyNodeList(Node);
       xmlAddChild(root_node, disk_node);
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char *) "application/xml";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_media_filesystem_disk_req(ngx_http_request_t *r,xmlDocPtr doc,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlChar* attr_value   = NULL;

    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode) {
       return NGX_ERROR;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST COMMON_XML_REQ)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media manage deal operate request,req not found.");
       return NGX_ERROR;
    }
    /* disk node */
    curNode = curNode->children;

    if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_NODE_DISK)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media manage deal operate request,operate not found.");
       return NGX_ERROR;
    }

    attr_value = xmlGetProp(curNode,BAD_CAST COMMON_XML_COMMAND);
    if(NULL == attr_value) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media manage deal operate request,type not found.");
        return NGX_ERROR;
    }


    if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_STAT)) {
        return ngx_media_filesystem_disk_stat(r,out);
    }
    else if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_LIST)) {
        return ngx_media_filesystem_disk_list(r,curNode,out);
    }
    else if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_DEL)) {
        return ngx_media_filesystem_disk_delete(r,curNode,out);
    }
    else if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_MKDIR)) {
        return ngx_media_filesystem_disk_mkdir(r,curNode,out);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media  request operate type is invalid.");
    return NGX_ERROR;
}


static ngx_int_t
ngx_media_filesystem_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out)
{
    xmlDocPtr doc;
    int ret = 0;
    xmlNodePtr curNode  = NULL;

    xmlKeepBlanksDefault(0);
    doc = xmlReadDoc((const xmlChar *)req_xml,"msg_req.xml",NULL,0);
    if(NULL == doc) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media system deal the xml fail.");
        return NGX_ERROR;
    }


    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media system get the xml root fail.");
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST COMMON_XML_REQ)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media system find the xml req node fail.");
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    curNode = curNode->children;
    if (!xmlStrcmp(curNode->name, BAD_CAST SYSTEM_NODE_DISK)) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "ngx media system find the xml disk node :%s.",curNode->name);
        ret = ngx_media_filesystem_disk_req(r,doc,out);

    }
    else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media system unknow xml node :%s.",curNode->name);
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    xmlFreeDoc(doc);
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "ngx ngx media task deal xml req,ret:%d.",ret);
    return ret;
}
static void
ngx_media_filesystem_recv_request(ngx_http_request_t *r)
{
    int ret;
    u_char       *p;
    u_char       *req;
    size_t        len;
    ngx_buf_t    *buf;
    ngx_chain_t  *cl;
    ngx_chain_t  out;

    if (r->request_body == NULL
        || r->request_body->bufs == NULL
        || r->request_body->temp_file)
    {
        return ;
    }

    cl = r->request_body->bufs;
    buf = cl->buf;

    len = buf->last - buf->pos;
    cl = cl->next;

    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        len += buf->last - buf->pos;
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video manager ,alloc the buf fail.");
        return ;
    }
    req = p;

    cl = r->request_body->bufs;
    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    }

    *p = '\0';

    //r->request_body
    out.buf = NULL;
    ret = ngx_media_filesystem_deal_xml_req(r,(const char * )req,&out);
    if(NGX_OK != ret){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video manager ,deal xml request fail,xml:'%s'.",req,&out);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
    r->headers_out.status = NGX_HTTP_OK;
    r->keepalive   = 0;

    if(NULL == out.buf) {
        r->header_only = 1;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, NGX_DONE);
    }
    else {
        r->header_only = 0;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
    }
    return ;
}


