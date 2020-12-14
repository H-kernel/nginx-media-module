/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_schd_zk_module.c
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
#include "ngx_schd.h"

       

typedef struct {
    zhandle_t                      *sch_zk_handle;
    char                           *str_zk_address;
}ngx_schd_zk_info_t;

typedef struct {
    ngx_schd_zk_info_t             *zk_info;
    ngx_uint_t                      type;
    char                           *node_path;
    ngx_log_t                      *log;
} ngx_schd_node_info_t;


typedef struct {
    ngx_str_t                       sch_zk_address;
    ngx_msec_t                      sch_zk_update;
    
    ngx_schd_zk_info_t              zk_info;
    ngx_array_t                    *node_array;
    ngx_event_t                     sch_zk_timer;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
} ngx_schd_zk_main_conf_t;

static void*     ngx_schd_zk_create_conf(ngx_cycle_t *cycle);
static char*     ngx_schd_zk_init_conf(ngx_cycle_t *cycle, void *conf);
static ngx_int_t ngx_schd_zk_init_process(ngx_cycle_t *cycle);
static void      ngx_schd_zk_exit_process(ngx_cycle_t *cycle);

/*************************zookeeper schedule****************************/
static void      ngx_schd_zk_root_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_schd_zk_root_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_schd_zk_sub_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_schd_zk_sub_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data);
static void      ngx_schd_zk_parent_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_schd_zk_parent_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_schd_zk_sub_check_all(ngx_schd_zk_main_conf_t* conf);
static void      ngx_schd_zk_sync_system_info(ngx_schd_node_info_t* node, const char *value, int value_len);
static void      ngx_schd_zk_watcher(zhandle_t *zh, int type,int state, const char *path,void *ctx);
static void      ngx_schd_zk_init_timer(ngx_schd_zk_main_conf_t* conf);
static void      ngx_schd_zk_check_timeout(ngx_event_t *ev);
static void      ngx_schd_zk_invoke_stat(ngx_schd_node_info_t* node);
static void      ngx_schd_zk_stat_completion_t(int rc, const struct Stat *stat,const void *data);



static ngx_command_t  ngx_schd_zk_commands[] = {

    { ngx_string(NGX_MEDIA_SCH_ZK_ADDR),
      NGX_SCHD_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_schd_zk_main_conf_t, sch_zk_address),
      NULL },


    { ngx_string(NGX_MEDIA_SCH_ZK_UPDATE),
      NGX_SCHD_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_schd_zk_main_conf_t, sch_zk_update),
      NULL },

      ngx_null_command
};


static ngx_schd_module_t  ngx_schd_zk_module_ctx = {
    ngx_schd_zk_create_conf,
    ngx_schd_zk_init_conf,
};


ngx_module_t  ngx_schd_zk_module = {
    NGX_MODULE_V1,
    &ngx_schd_zk_module_ctx,             /* module context */
    ngx_schd_zk_commands,                /* module directives */
    NGX_SCHD_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    ngx_schd_zk_init_process,            /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    ngx_schd_zk_exit_process,            /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


static void*     
ngx_schd_zk_create_conf(ngx_cycle_t *cycle)
{
    ngx_schd_zk_main_conf_t  *zkcf;

    zkcf = ngx_pcalloc(cycle->pool, sizeof(ngx_schd_zk_main_conf_t));
    if (zkcf == NULL) {
        return NULL;
    }

    zkcf->node_array = ngx_array_create(cycle->pool, 
                                        NGX_ALLMEDIA_TYPE_MAX,
                                        sizeof(ngx_schd_node_info_t));
    if(NULL == zkcf->node_array) {
        return NULL;
    }
    
    ngx_str_null(&zkcf->sch_zk_address);
    zkcf->sch_zk_update      = NGX_CONF_UNSET;

    zkcf->zk_info.sch_zk_handle      = NULL;
    zkcf->zk_info.str_zk_address     = NULL;    

    zkcf->log                         = NULL;
    zkcf->pool                        = NULL;
    
    return zkcf;
}
static char*     
ngx_schd_zk_init_conf(ngx_cycle_t *cycle, void *conf)
{
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_schd_zk_init_process(ngx_cycle_t *cycle)
{
    ngx_schd_zk_main_conf_t        *zkcf       = NULL;
    ngx_schd_core_conf_t           *sccf       = NULL;
    ngx_schd_node_info_t           *node       = NULL;
    u_char                         *last       = NULL;

    zkcf = (ngx_schd_zk_main_conf_t *)ngx_schd_get_cycle_conf(cycle, ngx_schd_zk_module);
    if(NULL == zkcf) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_schd_zk_module: Fail to zk configuration");
        return NGX_OK;
    }
    sccf = (ngx_schd_core_conf_t *)ngx_schd_get_cycle_conf(cycle, ngx_schd_core_module);
    if(NULL == sccf) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_schd_zk_module: Fail to core configuration");
        return NGX_OK;
    }

    zkcf->log   = cycle->log;
    zkcf->pool  = cycle->pool;

    /* execs zookeeper are always started by the first worker */
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_schd_zk_module,the process:[%d] is not 0,no need start zookeepr.",ngx_process_slot);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "ngx_schd_zk_module,the process:[%d] is 0,so start zookeepr.",ngx_process_slot);


    if(0 >= zkcf->sch_zk_address.len) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_zk_module: NO zookeeper address,so no need register");
        return NGX_OK;
    }

    /* init the zookeepr */
    if(NULL == zkcf->zk_info.str_zk_address) {
        zkcf->zk_info.str_zk_address = ngx_pcalloc(cycle->pool,
                                              zkcf->sch_zk_address.len+1);
    }

    if(NULL == zkcf->zk_info.str_zk_address) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_zk_module: create the zk info fail,so no need register");
        return NGX_OK;
    }

    last = ngx_cpymem((u_char *)zkcf->zk_info.str_zk_address,
                 zkcf->sch_zk_address.data,
                 zkcf->sch_zk_address.len);
    *last = '\0';


    if(NGX_ALLMEDIA_TYPE_TRANSCODE == (sccf->sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE)) {
        node = ngx_array_push(zkcf->node_array);
        if(NULL == node) {
            ngx_log_stderr(0, "ngx_schd_zk_module: Fail to create transcode node");
            return NGX_ERROR;
        }
        node->type       = NGX_ALLMEDIA_TYPE_TRANSCODE;
        node->zk_info    = &zkcf->zk_info;
        node->log        = cycle->log;
        node->node_path  = ngx_pcalloc(cycle->pool,
                                      sccf->sch_zk_server_id.len
                                      + ngx_strlen(NGX_MEDIA_SCH_ZK_TRANSCODE)+ 2);
        last = ngx_cpymem((u_char *)node->node_path,
                 NGX_MEDIA_SCH_ZK_TRANSCODE,ngx_strlen(NGX_MEDIA_SCH_ZK_TRANSCODE));
        *last = '/';
        last++;
        last = ngx_cpymem(last,sccf->sch_zk_server_id.data,
                                sccf->sch_zk_server_id.len);
        *last = '\0';
    }

    if(NGX_ALLMEDIA_TYPE_ACCESS == (sccf->sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS)) {
        node = ngx_array_push(zkcf->node_array);
        if(NULL == node) {
            ngx_log_stderr(0, "ngx_schd_zk_module: Fail to create access node");
            return NGX_ERROR;
        }
        node->type       = NGX_ALLMEDIA_TYPE_ACCESS;
        node->zk_info    = &zkcf->zk_info;
        node->log        = cycle->log;
        node->node_path  = ngx_pcalloc(cycle->pool,
                                        sccf->sch_zk_server_id.len
                                        + ngx_strlen(NGX_MEDIA_SCH_ZK_ACCESS)+ 2);
        last = ngx_cpymem((u_char *)node->node_path,
                 NGX_MEDIA_SCH_ZK_ACCESS,ngx_strlen(NGX_MEDIA_SCH_ZK_ACCESS));
        *last = '/';
        last++;
        last = ngx_cpymem(last,sccf->sch_zk_server_id.data,
                               sccf->sch_zk_server_id.len);
        *last = '\0';
    }

    if(NGX_ALLMEDIA_TYPE_STREAM == (sccf->sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM)) {
        node = ngx_array_push(zkcf->node_array);
        if(NULL == node) {
            ngx_log_stderr(0, "ngx_schd_zk_module: Fail to create stream node");
            return NGX_ERROR;
        }
        node->type       = NGX_ALLMEDIA_TYPE_STREAM;
        node->zk_info    = &zkcf->zk_info;
        node->log        = cycle->log;
        node->node_path  = ngx_pcalloc(cycle->pool,
                                        sccf->sch_zk_server_id.len
                                        + ngx_strlen(NGX_MEDIA_SCH_ZK_STREAM)+ 2);
        last = ngx_cpymem((u_char *)node->node_path,
                 NGX_MEDIA_SCH_ZK_STREAM,ngx_strlen(NGX_MEDIA_SCH_ZK_STREAM));
        *last = '/';
        last++;
        last = ngx_cpymem(last,sccf->sch_zk_server_id.data,
                               sccf->sch_zk_server_id.len);
        *last = '\0';
    }

    if(NGX_ALLMEDIA_TYPE_STORAGE == (sccf->sch_server_flags&NGX_ALLMEDIA_TYPE_STORAGE)) {
         node = ngx_array_push(zkcf->node_array);
        if(NULL == node) {
            ngx_log_stderr(0, "ngx_schd_zk_module: Fail to create storage node");
            return NGX_ERROR;
        }
        node->type       = NGX_ALLMEDIA_TYPE_STORAGE;
        node->zk_info    = &zkcf->zk_info;
        node->log        = cycle->log;
        node->node_path  = ngx_pcalloc(cycle->pool,
                                        sccf->sch_zk_server_id.len
                                        + ngx_strlen(NGX_MEDIA_SCH_ZK_STORAGE)+ 2);
        last = ngx_cpymem((u_char *)node->node_path,
                 NGX_MEDIA_SCH_ZK_STORAGE,ngx_strlen(NGX_MEDIA_SCH_ZK_STORAGE));
        *last = '/';
        last++;
        last = ngx_cpymem(last,sccf->sch_zk_server_id.data,
                               sccf->sch_zk_server_id.len);
        *last = '\0';
    }


#if (NGX_DEBUG)
    zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);
#else
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
#endif
    zkcf->zk_info.sch_zk_handle
        = zookeeper_init(zkcf->zk_info.str_zk_address, ngx_schd_zk_watcher, 10000, 0, zkcf, 0);
    if(NULL == zkcf->zk_info.sch_zk_handle) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Fail to init zookeeper instance");
        ngx_log_stderr(0, "Fail to init zookeeper instance");
        return NGX_ERROR;
    }

    ngx_schd_zk_init_timer(zkcf);
    return NGX_OK;
}

static void
ngx_schd_zk_exit_process(ngx_cycle_t *cycle)
{
    return ;
}

static void
ngx_schd_zk_root_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_schd_zk_main_conf_t* conf = (ngx_schd_zk_main_conf_t*)data;
    if(rc != ZOK) {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,"the zookeeper root path is not exsti,so create it,error info:%s",zerror(rc));
        zoo_acreate(conf->zk_info.sch_zk_handle, NGX_MEDIA_SCH_ZK_ROOT, NULL, -1,
                        &ZOO_OPEN_ACL_UNSAFE, 0,
                        ngx_schd_zk_root_create_completion_t, conf);
        return;

    }
    
    ngx_log_error(NGX_LOG_INFO, conf->log, 0,"check the zookeeper allmedia path success,so check the child path.");
    ngx_schd_zk_sub_check_all(conf);
    return;
}

static void
ngx_schd_zk_root_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_schd_zk_main_conf_t* conf
              = (ngx_schd_zk_main_conf_t *)data;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_schd_zk_root_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        if(NULL != value) {
            ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper root node:%s",value);
        }
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"create the zookeeper allmedia path success,so check the child path.");
        ngx_schd_zk_sub_check_all(conf);
    }
    else{
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create zookeeper root node");
        zookeeper_close(conf->zk_info.sch_zk_handle);
        conf->zk_info.sch_zk_handle = NULL;
    }
}

static void
ngx_schd_zk_sub_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_schd_node_info_t* node   = (ngx_schd_node_info_t*)data;

    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_sub_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        if(NULL != value) {
            ngx_log_error(NGX_LOG_INFO, node->log, 0, "create zookeeper tranacode node:%s",value);
        }
        /* try update the service stat info */
        ngx_schd_zk_invoke_stat(node);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, node->log, 0, "Fail to create zookeeper node");
        zookeeper_close(node->zk_info->sch_zk_handle);
        node->zk_info->sch_zk_handle = NULL;
    }

}

static void
ngx_schd_zk_sub_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data)
{
    ngx_schd_node_info_t* node   = (ngx_schd_node_info_t*)data;
    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_sub_get_completion_t:ret:[%d].",rc);

    if(rc == ZOK)
    {
       if(NULL != node->node_path){
           ngx_schd_zk_sync_system_info(node,value,value_len);
       }
    }
    else {
        zookeeper_close(node->zk_info->sch_zk_handle);
        node->zk_info->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, node->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}
static void      
ngx_schd_zk_parent_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_schd_node_info_t* node   = (ngx_schd_node_info_t*)data;
    int nRet =  ZOK;

    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_parent_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        if(NULL != value) {
            ngx_log_error(NGX_LOG_INFO, node->log, 0, "create zookeeper parent:%s",value);
        }
        ngx_log_error(NGX_LOG_INFO, node->log, 0,"create the zookeeper parent path success,so create the child path.");
        if(NULL != node->node_path){
            nRet = zoo_acreate(node->zk_info->sch_zk_handle, node->node_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_schd_zk_sub_create_completion_t, node);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, node->log, 0,"create sub node path fail,so disconnect and try again later.");
                zookeeper_close(node->zk_info->sch_zk_handle);
                node->zk_info->sch_zk_handle = NULL;
            }
        }
    }
    else {
        ngx_log_error(NGX_LOG_ERR, node->log, 0, "Fail to create parent node");
        zookeeper_close(node->zk_info->sch_zk_handle);
        node->zk_info->sch_zk_handle = NULL;

    }
}
static void      
ngx_schd_zk_parent_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_schd_node_info_t* node    = (ngx_schd_node_info_t*)data;
    char                 *path    = NULL;
    int nRet =  ZOK;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, node->log, 0,"check the zookeeper node parent path success,so create the child path.");
        // create transcode node
        if(NULL != node->node_path){
            nRet = zoo_acreate(node->zk_info->sch_zk_handle, node->node_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_schd_zk_sub_create_completion_t, node);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, node->log, 0,"create sub node path fail,so disconnect and try again later.");
                zookeeper_close(node->zk_info->sch_zk_handle);
                node->zk_info->sch_zk_handle = NULL;
            }
        }
    }
    else {
        
        ngx_log_error(NGX_LOG_WARN, node->log, 0,"the zookeeper parent path is not exsti,so create it,error info:%s",zerror(rc));
        if(NGX_ALLMEDIA_TYPE_TRANSCODE == node->type) {
            path = NGX_MEDIA_SCH_ZK_TRANSCODE;
        }
        else if(NGX_ALLMEDIA_TYPE_ACCESS == node->type){
            path = NGX_MEDIA_SCH_ZK_ACCESS;
        }
        else if(NGX_ALLMEDIA_TYPE_STREAM == node->type){
            path = NGX_MEDIA_SCH_ZK_STREAM;
        }
        else if(NGX_ALLMEDIA_TYPE_STORAGE == node->type){
            path = NGX_MEDIA_SCH_ZK_STORAGE;
        }
        else {
            return;
        }
        zoo_acreate(node->zk_info->sch_zk_handle, path, NULL, -1,
                        &ZOO_OPEN_ACL_UNSAFE, 0,
                        ngx_schd_zk_parent_create_completion_t, node);

    }
}
static void      
ngx_schd_zk_sub_check_all(ngx_schd_zk_main_conf_t* conf)
{
    ngx_schd_node_info_t      *node    = NULL;
    ngx_uint_t                 i       = 0;
    char                      *path    = NULL;
    node = (ngx_schd_node_info_t*)conf->node_array->elts;
    for(i = 0;i < conf->node_array->nelts;i++) {
        if(NGX_ALLMEDIA_TYPE_TRANSCODE == node[i].type) {
            path = NGX_MEDIA_SCH_ZK_TRANSCODE;
        }
        else if(NGX_ALLMEDIA_TYPE_ACCESS == node[i].type){
            path = NGX_MEDIA_SCH_ZK_ACCESS;
        }
        else if(NGX_ALLMEDIA_TYPE_STREAM == node[i].type){
            path = NGX_MEDIA_SCH_ZK_STREAM;
        }
        else if(NGX_ALLMEDIA_TYPE_STORAGE == node[i].type){
            path = NGX_MEDIA_SCH_ZK_STORAGE;
        }
        else {
            continue;
        }
        // check sub node
        int nRet = zoo_aexists(conf->zk_info.sch_zk_handle,path,0,
                               ngx_schd_zk_parent_exsit_completion_t,&node[i]);
        if(ZOK != nRet) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,"exist sub transcode path fail,so disconnect and try again later.");
            zookeeper_close(conf->zk_info.sch_zk_handle);
            conf->zk_info.sch_zk_handle = NULL;
        }
    }
    return;
}

static void
ngx_schd_zk_watcher(zhandle_t *zh, int type,int state, const char *path,void *ctx)
{
    ngx_schd_zk_main_conf_t* conf
                = (ngx_schd_zk_main_conf_t*)ctx;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_schd_zk_watcher: stat:[%d]!",state);

    if(type == ZOO_SESSION_EVENT) {
        if(state == ZOO_CONNECTED_STATE) {
            //check allmedia root path
            int nRet = zoo_aexists(conf->zk_info.sch_zk_handle,
                                   NGX_MEDIA_SCH_ZK_ROOT,0,ngx_schd_zk_root_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create root path fail,so disconnect and try again later.");
                zookeeper_close(conf->zk_info.sch_zk_handle);
                conf->zk_info.sch_zk_handle = NULL;
            }

        }
        else if( state == ZOO_AUTH_FAILED_STATE) {
            zookeeper_close(zh);
            conf->zk_info.sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "zookeeper Authentication failure");
        }
        else if( state == ZOO_EXPIRED_SESSION_STATE) {
            zookeeper_close(zh);
            conf->zk_info.sch_zk_handle
                    = zookeeper_init(conf->zk_info.str_zk_address,
                                     ngx_schd_zk_watcher,
                                     10000, 0, conf, 0);
            if (NULL == conf->zk_info.sch_zk_handle) {
                ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to init zookeeper instance");
            }
        }
    }
}

static void
ngx_schd_zk_init_timer(ngx_schd_zk_main_conf_t* conf)
{
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_schd_zk_module:start zookeeper register timer.");
    ngx_memzero(&conf->sch_zk_timer, sizeof(ngx_event_t));
    conf->sch_zk_timer.handler = ngx_schd_zk_check_timeout;
    conf->sch_zk_timer.log     = conf->log;
    conf->sch_zk_timer.data    = conf;

    ngx_add_timer(&conf->sch_zk_timer, conf->sch_zk_update);
}

static void
ngx_schd_zk_check_timeout(ngx_event_t *ev)
{
    ngx_schd_zk_main_conf_t* conf
            = (ngx_schd_zk_main_conf_t*)ev->data;
    
    ngx_schd_node_info_t      *node    = NULL;
    ngx_uint_t                 i       = 0;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_schd_zk_module:update zookeeper register info.");

    if(NULL == conf->zk_info.sch_zk_handle) {
        conf->zk_info.sch_zk_handle
                    = zookeeper_init(conf->zk_info.str_zk_address,
                                     ngx_schd_zk_watcher,
                                     10000, 0, conf, 0);
        if (NULL == conf->zk_info.sch_zk_handle) {
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to init zookeeper instance");
        }
    }
    else {
        node = (ngx_schd_node_info_t*)conf->node_array->elts;
        for(i = 0;i < conf->node_array->nelts;i++) {
            ngx_schd_zk_invoke_stat(&node[i]);
        }
    }
    ngx_add_timer(&conf->sch_zk_timer, conf->sch_zk_update);
}
static void
ngx_schd_zk_sync_system_info(ngx_schd_node_info_t* node, const char *value, int value_len)
{
 
    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_sync_system_info,begin");

    cJSON *root = cJSON_Parse(value);
    if(root == NULL)
    {
        root = cJSON_CreateObject();
    }

    if(NGX_OK != ngx_schd_zk_get_system_info_json(root)) {
        cJSON_Delete(root);
        return;
    }

    /* build the json infomation and update the zookeeper node */
    char * json_buf = (char*)cJSON_PrintUnformatted(root);
    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_update_stat:value:[%s].",json_buf);

    zoo_aset(node->zk_info->sch_zk_handle,node->node_path,
             json_buf,ngx_strlen(json_buf),-1,
             ngx_schd_zk_stat_completion_t,node);

    free(json_buf);

    cJSON_Delete(root);
    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_sync_system_info,end");
    return;
}

static void
ngx_schd_zk_stat_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_schd_node_info_t* node = (ngx_schd_node_info_t*)data;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_DEBUG, node->log, 0,"update to service data to zookeeper success.");
    }
    else {
        zookeeper_close(node->zk_info->sch_zk_handle);
        node->zk_info->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, node->log, 0,"update to upstream workload is failed,error info:%s",zerror(rc));
    }
}
static void
ngx_schd_zk_invoke_stat(ngx_schd_node_info_t* node)
{
    int rc;

    ngx_log_error(NGX_LOG_DEBUG, node->log, 0, "ngx_schd_zk_invoke_stat.");

    if(NULL == node->zk_info->sch_zk_handle) {
        ngx_log_error(NGX_LOG_ERR, node->log, 0, "invoke the stat info ,but the zk handle fail.");
        return;
    }

    if(zoo_state(node->zk_info->sch_zk_handle) != ZOO_CONNECTED_STATE) {
        return;
    }

    if(NULL != node->node_path) {
        rc = zoo_aget(node->zk_info->sch_zk_handle,node->node_path, 0,
                        ngx_schd_zk_sub_get_completion_t, node);
        if(rc != ZOK) {
            zookeeper_close(node->zk_info->sch_zk_handle);
            node->zk_info->sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, node->log, 0, "get node path is failed, %s,error info:%s",node->node_path,zerror(rc));
            return;
        }
    }
}


