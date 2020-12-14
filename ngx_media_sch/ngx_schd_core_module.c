/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_schd_core_module.c
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
#include "ngx_media_include.h"
#include "ngx_media_sys_stat.h"
#include "ngx_media_common.h"
#include "ngx_media_license_module.h"
#include "ngx_schd.h"
#include <common/as_json.h>

static ngx_schd_core_conf_t* schd_cf = NULL;

static void*     ngx_schd_core_create_conf(ngx_cycle_t *cycle);
static char*     ngx_schd_core_init_conf(ngx_cycle_t *cycle, void *conf);
static char*     ngx_schd_core_signal_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char*     ngx_schd_core_service_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_int_t ngx_schd_core_init_process(ngx_cycle_t *cycle);
static void      ngx_schd_core_exit_process(ngx_cycle_t *cycle);


static ngx_conf_bitmask_t  ngx_media_schd_type_mask[] = {
    { ngx_string("all"),                NGX_ALLMEDIA_TYPE_ALL       },
    { ngx_string("transcode"),          NGX_ALLMEDIA_TYPE_TRANSCODE },
    { ngx_string("access"),             NGX_ALLMEDIA_TYPE_ACCESS    },
    { ngx_string("stream"),             NGX_ALLMEDIA_TYPE_STREAM    },
    { ngx_string("storage"),            NGX_ALLMEDIA_TYPE_STORAGE   },
    { ngx_null_string,                  0                           }
};



static ngx_command_t  ngx_schd_core_commands[] = {

    { ngx_string(NGX_MEDIA_SCH_SERVERID),
      NGX_SCHD_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_schd_core_conf_t, sch_zk_server_id),
      NULL },

    { ngx_string(NGX_MEDIA_SCH_SERTYPE),
      NGX_SCHD_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      0,
      offsetof(ngx_schd_core_conf_t, sch_server_flags),
      ngx_media_schd_type_mask },


    { ngx_string(NGX_MEDIA_SCH_SIGIP),
      NGX_SCHD_CONF|NGX_CONF_1MORE,
      ngx_schd_core_signal_address,
      0,
      offsetof(ngx_schd_core_conf_t, sch_signal_ip),
      NULL },

    { ngx_string(NGX_MEDIA_SCH_SERIP),
      NGX_SCHD_CONF|NGX_CONF_1MORE,
      ngx_schd_core_service_address,
      0,
      offsetof(ngx_schd_core_conf_t, sch_service_ip),
      NULL },
    
    { ngx_string(NGX_MEDIA_SCH_DISK),
      NGX_SCHD_CONF|NGX_CONF_1MORE,
      ngx_conf_set_keyval_slot,
      0,
      offsetof(ngx_schd_core_conf_t, sch_disk),
      NULL },

      ngx_null_command
};


static ngx_schd_module_t  ngx_schd_core_module_ctx = {
    ngx_schd_core_create_conf,
    ngx_schd_core_init_conf,
};


ngx_module_t  ngx_schd_core_module = {
    NGX_MODULE_V1,
    &ngx_schd_core_module_ctx,             /* module context */
    ngx_schd_core_commands,                /* module directives */
    NGX_SCHD_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_schd_core_init_process,            /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_schd_core_exit_process,            /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void*     
ngx_schd_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_schd_core_conf_t  *sccf;

    sccf = ngx_pcalloc(cycle->pool, sizeof(ngx_schd_core_conf_t));
    if (sccf == NULL) {
        return NULL;
    }

    
    
    ngx_str_null(&sccf->sch_zk_server_id);
    sccf->sch_server_flags   = 0;
    ngx_str_null(&sccf->sch_signal_ip.key);    
    ngx_str_null(&sccf->sch_signal_ip.value);
    ngx_str_null(&sccf->sch_service_ip.key);
    ngx_str_null(&sccf->sch_service_ip.value);

    sccf->sch_disk = ngx_array_create(cycle->pool, TRANS_VPATH_KV_MAX,
                                                sizeof(ngx_keyval_t));
    if (sccf->sch_disk == NULL) {
        return NULL;
    }
    
    return sccf;
}
static char*     
ngx_schd_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    return NGX_CONF_OK;
}

static char*
ngx_schd_core_signal_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                    *value;
    ngx_uint_t                    n;
    ngx_schd_core_conf_t         *sccf = NULL;
    sccf = (ngx_schd_core_conf_t *)ngx_schd_get_conf(cf, ngx_schd_core_module);
    if(NULL == sccf) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    n     = cf->args->nelts;

    if((2 != n)&&(4 !=n)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "schd core signal address conf count:[%uD] error.",n);
        return NGX_CONF_ERROR;
    }
    sccf->sch_signal_ip.key = value[1];
    if(2 == n) {
        return NGX_CONF_OK;
    }

    if (ngx_strncmp(value[2].data, (u_char *) "nat", 3) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "schd core signal address conf count:[%uD] is not net configure.",n);
        return NGX_CONF_ERROR;
    }
    sccf->sch_signal_ip.value= value[3];
    return NGX_CONF_OK;
}
static char*
ngx_schd_core_service_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                    *value;
    ngx_uint_t                    n;
    ngx_schd_core_conf_t         *sccf = NULL;
    sccf = (ngx_schd_core_conf_t *)ngx_schd_get_conf(cf, ngx_schd_core_module);
    if(NULL == sccf) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    n     = cf->args->nelts;

    if((2 != n)&&(4 !=n)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "schd service address conf count:[%uD] error.",n);
        return NGX_CONF_ERROR;
    }
    sccf->sch_service_ip.key = value[1];
    if(2 == n) {
        return NGX_CONF_OK;
    }

    if (ngx_strncmp(value[2].data, (u_char *) "nat", 3) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "schd service address conf count:[%uD] is not net configure.",n);
        return NGX_CONF_ERROR;
    }
    sccf->sch_service_ip.value= value[3];
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_schd_core_init_process(ngx_cycle_t *cycle)
{
    ngx_schd_core_conf_t           *sccf       = NULL;
    ngx_uint_t                      i          = 0;
    ngx_keyval_t                   *kv_diskInfo=NULL;
    u_char                          name[TRANS_VPATH_KV_MAX];
    u_char                          value[TRANS_VPATH_KV_MAX];
    u_char                         *last       = NULL;
    ngx_memzero(&name,TRANS_VPATH_KV_MAX);
    ngx_memzero(&value,TRANS_VPATH_KV_MAX);

    sccf = (ngx_schd_core_conf_t *) ngx_schd_get_cycle_conf(cycle, ngx_schd_core_module);
    if(NULL == sccf) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_schd_core_module: get the host conf fail.");
        return NGX_OK;
    }

    schd_cf = sccf;

    /* start host */
    if(NGX_OK != ngx_media_sys_stat_init(cycle)) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_core_module: init the host stat fail.");
        return NGX_ERROR;
    }

    /* add the host info */
    /* add the network infto to stat */
    if((0 < sccf->sch_signal_ip.key.len) &&(NULL != sccf->sch_signal_ip.key.data)) {
        last = ngx_cpymem(name,sccf->sch_signal_ip.key.data,sccf->sch_signal_ip.key.len);
        *last = '\0';
        if(NGX_OK != ngx_media_sys_stat_add_networdcard(name)){
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_core_module: add the network card:[%s] fail.",name);
            return NGX_OK;
        }
    }

    if((0 < sccf->sch_service_ip.key.len) &&(NULL != sccf->sch_service_ip.key.data)) {
        last = ngx_cpymem(name,sccf->sch_service_ip.key.data,sccf->sch_service_ip.key.len);
        *last = '\0';
        if(NGX_OK != ngx_media_sys_stat_add_networdcard(name)){
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_core_module: add the network card:[%s] fail.",name);
            return NGX_OK;
        }
    }


    /* add the disk info to stat */
    if(sccf->sch_disk) {
        kv_diskInfo = sccf->sch_disk->elts;
        for(i = 0;i < sccf->sch_disk->nelts;i++) {
            last = ngx_cpymem(name,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
            *last = '\0';
            last = ngx_cpymem(value,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            if(NGX_OK != ngx_media_sys_stat_add_disk(name,value)){
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_schd_core_module: add the disk name:[%s] path:[%s] fail.",name,value);
                return NGX_OK;
            }
        }
    }
    return NGX_OK;
}

static void
ngx_schd_core_exit_process(ngx_cycle_t *cycle)
{
    ngx_media_sys_stat_release(cycle);
    return ;
}

ngx_int_t 
ngx_schd_zk_get_system_info_json(cJSON *root)
{
    ngx_uint_t i              = 0;
    ngx_uint_t ullCpuPer      = 0;
    ngx_uint_t ullMemtotal    = 0;
    ngx_uint_t ullMemused     = 0;
    ngx_uint_t ulTotalSize    = 0;
    ngx_uint_t ulUsedRecvSize = 0;
    ngx_uint_t ulUsedSendSize = 0;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    u_char    *last           = NULL;
    ngx_keyval_t  *kv_diskInfo=NULL;

    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);

    if(root == NULL)
    {
        return NGX_ERROR;
    }

    if(NULL  == schd_cf) {
        return NGX_ERROR;
    }

    cJSON *taskObj,*taskCount,*taskMax;
    cJSON *RtmpObj,*RtmpCount,*RtmpMax;
    cJSON *HlsObj,*HlsCount,*HlsMax;
    cJSON *RtspObj,*RtspCount,*RtspMax;
    cJSON *cpuObj, *memObj,*memTotalObj,*memUsedObj,*memUnitObj;
    cJSON *signalipObj,*serviceipObj,*ipObj,*netObj,*ipUnitObj;
    cJSON *totalSize,*recvSize,*sendSize;
    cJSON *diskList,*diskObj;
    cJSON *vpath,*path,*diskSize,*usedSize,*diskUnitObj;


    ngx_uint_t task_count = ngx_media_license_task_current();
    ngx_uint_t task_max   = ngx_media_license_task_max();

    ngx_uint_t rtmp_count = ngx_media_license_rtmp_current();
    ngx_uint_t rtmp_max   = ngx_media_license_rtmp_channle();

    ngx_uint_t hls_count  = ngx_media_license_hls_current();
    ngx_uint_t hls_max    = ngx_media_license_hls_channle();

    ngx_uint_t rtsp_count = ngx_media_license_rtsp_current();
    ngx_uint_t rtsp_max   = ngx_media_license_rtsp_channle();

    /* 1.get the trans task ,rtmp,hls,rtsp info:total,capacity */
    /* task */
    taskObj = cJSON_GetObjectItem(root,"task");
    if(taskObj == NULL) {
        cJSON_AddItemToObject(root,"task",taskObj = cJSON_CreateObject());
    }
    taskCount = cJSON_GetObjectItem(taskObj,"taskcount");
    if(taskCount == NULL) {
        cJSON_AddItemToObject(taskObj,"taskcount",taskCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(taskCount, task_count);
    taskMax = cJSON_GetObjectItem(taskObj,"taskmax");
    if(taskMax == NULL) {
        cJSON_AddItemToObject(taskObj,"taskmax",taskMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(taskMax, task_max);
    /* rtmp */
    RtmpObj = cJSON_GetObjectItem(root,"rtmp");
    if(RtmpObj == NULL) {
        cJSON_AddItemToObject(root,"rtmp",RtmpObj = cJSON_CreateObject());
    }
    RtmpCount = cJSON_GetObjectItem(RtmpObj,"rtmpcount");
    if(RtmpCount == NULL) {
        cJSON_AddItemToObject(RtmpObj,"rtmpcount",RtmpCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtmpCount, rtmp_count);
    RtmpMax = cJSON_GetObjectItem(RtmpObj,"rtmpmax");
    if(RtmpMax == NULL) {
        cJSON_AddItemToObject(RtmpObj,"rtmpmax",RtmpMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtmpMax, rtmp_max);
    /* hls */
    HlsObj = cJSON_GetObjectItem(root,"hls");
    if(HlsObj == NULL) {
        cJSON_AddItemToObject(root,"hls",HlsObj = cJSON_CreateObject());
    }
    HlsCount = cJSON_GetObjectItem(HlsObj,"hlscount");
    if(HlsCount == NULL) {
        cJSON_AddItemToObject(HlsObj,"hlscount",HlsCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(HlsCount, hls_count);
    HlsMax = cJSON_GetObjectItem(HlsObj,"hlsmax");
    if(HlsMax == NULL) {
        cJSON_AddItemToObject(HlsObj,"hlsmax",HlsMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(HlsMax, hls_max);
    /* rtsp */
    RtspObj = cJSON_GetObjectItem(root,"rtsp");
    if(RtspObj == NULL) {
        cJSON_AddItemToObject(root,"rtsp",RtspObj = cJSON_CreateObject());
    }
    RtspCount = cJSON_GetObjectItem(RtspObj,"rtspcount");
    if(RtspCount == NULL) {
        cJSON_AddItemToObject(RtspObj,"rtspcount",RtspCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtspCount, rtsp_count);
    RtspMax = cJSON_GetObjectItem(RtspObj,"rtspmax");
    if(RtspMax == NULL) {
        cJSON_AddItemToObject(RtspObj,"rtspmax",RtspMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtspMax, rtsp_max);
    /* 2.get the system info:cpu,memory,disk,network */
    /*cpu,memory*/
    ngx_media_sys_stat_get_cpuinfo(&ullCpuPer);
    ngx_media_sys_stat_get_memoryinfo(&ullMemtotal,&ullMemused);

    cpuObj = cJSON_GetObjectItem(root,"cpu");

    if(cpuObj == NULL) {
        cJSON_AddItemToObject(root,"cpu",cpuObj = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(cpuObj, ullCpuPer);

    memObj = cJSON_GetObjectItem(root,"mem");
    if(memObj == NULL) {
        cJSON_AddItemToObject(root,"mem",memObj = cJSON_CreateObject());
    }

    memUnitObj = cJSON_GetObjectItem(memObj,"unit");
    if(memUnitObj == NULL) {
        cJSON_AddItemToObject(memObj,"unit",memUnitObj = cJSON_CreateString("KB"));
    }

    memTotalObj = cJSON_GetObjectItem(memObj,"total");
    if(memTotalObj == NULL) {
        cJSON_AddItemToObject(memObj,"total",memTotalObj = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(memTotalObj, ullMemtotal);

    memUsedObj = cJSON_GetObjectItem(memObj,"used");
    if(memUsedObj == NULL) {
        cJSON_AddItemToObject(memObj,"used",memUsedObj = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(memUsedObj, ullMemused);

    /*signal ip network*/
    if(0 < schd_cf->sch_signal_ip.key.len){
        signalipObj = cJSON_GetObjectItem(root,"signalip");
        last = ngx_cpymem(cbuf,schd_cf->sch_signal_ip.key.data,schd_cf->sch_signal_ip.key.len);
        *last = '\0';
        if(signalipObj == NULL){
            cJSON_AddItemToObject(root,"signalip",signalipObj = cJSON_CreateObject());
        }
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);

        ipObj = cJSON_GetObjectItem(signalipObj,"ip");
        if(ipObj == NULL) {
            cJSON_AddItemToObject(signalipObj,"ip",ipObj = cJSON_CreateString((char*)&cbuf[0]));
        }

        if((NULL != schd_cf->sch_signal_ip.value.data)
           &&(0 < schd_cf->sch_signal_ip.value.len)) {
            last = ngx_cpymem(cbuf,schd_cf->sch_signal_ip.value.data,schd_cf->sch_signal_ip.value.len);
            *last = '\0';

            netObj = cJSON_GetObjectItem(signalipObj,"nat");
            if(netObj == NULL){
                cJSON_AddItemToObject(signalipObj,"nat",netObj = cJSON_CreateString((char*)&cbuf[0]));
            }
        }

        ipUnitObj = cJSON_GetObjectItem(signalipObj,"unit");
        if(ipUnitObj == NULL) {
            cJSON_AddItemToObject(signalipObj,"unit",ipUnitObj = cJSON_CreateString("kbps"));
        }

        totalSize = cJSON_GetObjectItem(signalipObj,"total");
        if(totalSize == NULL) {
            cJSON_AddItemToObject(signalipObj,"total",totalSize = cJSON_CreateNumber(0));
        }
        recvSize = cJSON_GetObjectItem(signalipObj,"recv");
        if(recvSize == NULL) {
            cJSON_AddItemToObject(signalipObj,"recv",recvSize = cJSON_CreateNumber(0));
        }
        sendSize = cJSON_GetObjectItem(signalipObj,"send");
        if(sendSize == NULL) {
            cJSON_AddItemToObject(signalipObj,"send",sendSize = cJSON_CreateNumber(0));
        }

        cJSON_SetNumberValue(totalSize, ulTotalSize);
        cJSON_SetNumberValue(recvSize, ulUsedRecvSize);
        cJSON_SetNumberValue(sendSize, ulUsedSendSize);

    }

    /*service ip network*/
    if(0 < schd_cf->sch_service_ip.key.len){
        serviceipObj = cJSON_GetObjectItem(root,"serviceip");
        last = ngx_cpymem(cbuf,schd_cf->sch_service_ip.key.data,schd_cf->sch_service_ip.key.len);
        *last = '\0';
        if(serviceipObj == NULL){
            cJSON_AddItemToObject(root,"serviceip",serviceipObj = cJSON_CreateObject());
        }
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);


        ipObj = cJSON_GetObjectItem(serviceipObj,"ip");
        if(ipObj == NULL){
            cJSON_AddItemToObject(serviceipObj,"ip",ipObj = cJSON_CreateString((char*)&cbuf[0]));
        }

        if((NULL != schd_cf->sch_service_ip.value.data)
           &&(0 < schd_cf->sch_service_ip.value.len)) {
            last = ngx_cpymem(cbuf,schd_cf->sch_service_ip.value.data,schd_cf->sch_service_ip.value.len);
            *last = '\0';

            netObj = cJSON_GetObjectItem(serviceipObj,"nat");
            if(netObj == NULL){
                cJSON_AddItemToObject(serviceipObj,"nat",netObj = cJSON_CreateString((char*)&cbuf[0]));
            }
        }
        ipUnitObj = cJSON_GetObjectItem(serviceipObj,"unit");
        if(ipUnitObj == NULL){
            cJSON_AddItemToObject(serviceipObj,"unit",ipUnitObj = cJSON_CreateString("kbps"));
        }
        totalSize = cJSON_GetObjectItem(serviceipObj,"total");
        if(totalSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"total",totalSize = cJSON_CreateNumber(0));
        }
        recvSize = cJSON_GetObjectItem(serviceipObj,"recv");
        if(recvSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"recv",recvSize = cJSON_CreateNumber(0));
        }
        sendSize = cJSON_GetObjectItem(serviceipObj,"send");
        if(sendSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"send",sendSize = cJSON_CreateNumber(0));
        }

        cJSON_SetNumberValue(totalSize, ulTotalSize);
        cJSON_SetNumberValue(recvSize, ulUsedRecvSize);
        cJSON_SetNumberValue(sendSize, ulUsedSendSize);

    }

    /*disk stat info*/
    if(0 < schd_cf->sch_disk->nelts) {
        diskList = cJSON_GetObjectItem(root,"diskinfolist");
        if(diskList == NULL){
            cJSON_AddItemToObject(root,"diskinfolist",diskList = cJSON_CreateArray());
        }
        kv_diskInfo = schd_cf->sch_disk->elts;
        for(i = 0;i < schd_cf->sch_disk->nelts;i++) {
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            ngx_media_sys_stat_get_diskinfo(cbuf,&ulTotalSize,&ulUsedRecvSize);
            diskObj = cJSON_GetArrayItem(diskList,i);
            if(diskObj == NULL) {
                cJSON_AddItemToObject(diskList,"diskinfo",diskObj = cJSON_CreateObject());
            }

            vpath = cJSON_GetObjectItem(diskObj,"vptah");
            last = ngx_cpymem(cbuf,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
            *last = '\0';
            if(vpath == NULL) {
                cJSON_AddItemToObject(diskObj,"vptah",vpath = cJSON_CreateString((char*)&cbuf[0]));
            }
            path = cJSON_GetObjectItem(diskObj,"path");
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            if(path == NULL) {
                cJSON_AddItemToObject(diskObj,"path",vpath = cJSON_CreateString((char*)&cbuf[0]));
            }


            diskUnitObj = cJSON_GetObjectItem(diskObj,"unit");
            if(diskUnitObj == NULL) {
                cJSON_AddItemToObject(diskObj,"unit",diskUnitObj = cJSON_CreateString("MB"));
            }

            diskSize = cJSON_GetObjectItem(diskObj,"diskSize");
            if(diskSize == NULL) {
                cJSON_AddItemToObject(diskObj,"diskSize",diskSize = cJSON_CreateNumber(0));
            }

            usedSize = cJSON_GetObjectItem(diskObj,"usedSize");
            if(usedSize == NULL) {
                cJSON_AddItemToObject(diskObj,"usedSize",usedSize = cJSON_CreateNumber(0));
            }

            cJSON_SetNumberValue(diskSize, ulTotalSize);
            cJSON_SetNumberValue(usedSize, ulUsedRecvSize);
        }
    }

    return NGX_OK;
}
ngx_int_t ngx_schd_zk_get_system_info_xml(xmlDocPtr root)
{
    xmlNodePtr root_node   = NULL;
    xmlNodePtr system_node = NULL;
    xmlNodePtr task_node   = NULL,rtmp_node = NULL,hls_node = NULL,rtsp_node = NULL;
    xmlNodePtr cpu_node    = NULL,mem_node  = NULL,ip_node  = NULL;
    xmlNodePtr disk_node   = NULL;
    xmlNodePtr vpath_node  = NULL;      /* node pointers */
    u_char* last           = NULL;
    ngx_uint_t i              = 0;
    ngx_uint_t ulUsage        = 0;
    ngx_uint_t ulTotalSize    = 0;
    ngx_uint_t ulUsedSize     = 0;
    ngx_uint_t ulFreeSize     = 0;
    ngx_uint_t ulUsedRecvSize = 0;
    ngx_uint_t ulUsedSendSize = 0;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_keyval_t  *kv_diskInfo=NULL;

    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);

    if(NULL  == schd_cf) {
        return NGX_ERROR;
    }

    /* Creates a new document, a node and set it as a root node*/
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(root, root_node);
    
    /* system stat head */
    system_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_SYSTEM);
    xmlNewProp(system_node, BAD_CAST COMMON_XML_COMMAND, BAD_CAST SYSTEM_COMMAND_STAT);
    xmlAddChild(root_node, system_node);

    /* task rtmp hls rtsp stat info */
    ngx_uint_t task_count = ngx_media_license_task_current();
    ngx_uint_t task_max   = ngx_media_license_task_max();

    ngx_uint_t rtmp_count = ngx_media_license_rtmp_current();
    ngx_uint_t rtmp_max   = ngx_media_license_rtmp_channle();

    ngx_uint_t hls_count  = ngx_media_license_hls_current();
    ngx_uint_t hls_max    = ngx_media_license_hls_channle();

    ngx_uint_t rtsp_count = ngx_media_license_rtsp_current();
    ngx_uint_t rtsp_max   = ngx_media_license_rtsp_channle();
    
    /* task */
    task_node = xmlNewNode(NULL, BAD_CAST "task");
    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", task_count);
    *last = '\0';
    xmlNewProp(task_node, BAD_CAST "taskcount", BAD_CAST cbuf);

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", task_max);
    *last = '\0';
    xmlNewProp(task_node, BAD_CAST "taskmax", BAD_CAST cbuf);
    xmlAddChild(system_node, task_node);
    
    /* rtmp */
    rtmp_node = xmlNewNode(NULL, BAD_CAST "rtmp");
    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", rtmp_count);
    *last = '\0';
    xmlNewProp(rtmp_node, BAD_CAST "rtmpcount", BAD_CAST cbuf);

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", rtmp_max);
    *last = '\0';
    xmlNewProp(rtmp_node, BAD_CAST "rtmpmax", BAD_CAST cbuf);
    xmlAddChild(system_node, rtmp_node);
    
    /* hls */
    hls_node = xmlNewNode(NULL, BAD_CAST "hls");
    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", hls_count);
    *last = '\0';
    xmlNewProp(hls_node, BAD_CAST "hlscount", BAD_CAST cbuf);

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", hls_max);
    *last = '\0';
    xmlNewProp(hls_node, BAD_CAST "hlsmax", BAD_CAST cbuf);
    xmlAddChild(system_node, hls_node);

    /* rtsp */
    rtsp_node = xmlNewNode(NULL, BAD_CAST "rtsp");
    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", rtsp_count);
    *last = '\0';
    xmlNewProp(rtsp_node, BAD_CAST "rtspcount", BAD_CAST cbuf);

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", rtsp_max);
    *last = '\0';
    xmlNewProp(rtsp_node, BAD_CAST "rtspmax", BAD_CAST cbuf);
    xmlAddChild(system_node, rtsp_node);

    /* CPU info */
    ngx_media_sys_stat_get_cpuinfo(&ulUsage);   

    cpu_node = xmlNewNode(NULL, BAD_CAST "cpu");
    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsage);
    *last = '\0';
    xmlNewProp(cpu_node, BAD_CAST "usage", BAD_CAST cbuf);
    xmlAddChild(system_node, cpu_node);

    /* memory info */
    ngx_media_sys_stat_get_memoryinfo(&ulTotalSize,&ulUsedSize);

    mem_node = xmlNewNode(NULL, BAD_CAST "mem");
    xmlNewProp(mem_node, BAD_CAST "unit", BAD_CAST "KB");

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulTotalSize);
    *last = '\0';
    xmlNewProp(mem_node, BAD_CAST "total", BAD_CAST cbuf);

    last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsedSize);
    *last = '\0';
    xmlNewProp(mem_node, BAD_CAST "used", BAD_CAST cbuf);

    xmlAddChild(system_node, mem_node);

    /* signalip */
    if(0 < schd_cf->sch_signal_ip.key.len){
        ip_node = xmlNewNode(NULL, BAD_CAST "signalip");
        last = ngx_cpymem(cbuf,schd_cf->sch_signal_ip.key.data,schd_cf->sch_signal_ip.key.len);
        *last = '\0';
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);

        xmlNewProp(ip_node, BAD_CAST "ip", BAD_CAST cbuf);

        if((NULL != schd_cf->sch_signal_ip.value.data)&&(0 < schd_cf->sch_signal_ip.value.len)) {
            /* set the firewall address */
            last = ngx_cpymem(cbuf,schd_cf->sch_signal_ip.value.data,schd_cf->sch_signal_ip.value.len);
            *last = '\0';
            xmlNewProp(ip_node, BAD_CAST "nat", BAD_CAST cbuf);
        }

        xmlNewProp(ip_node, BAD_CAST "unit", BAD_CAST "kbps");

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulTotalSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "total", BAD_CAST cbuf);

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsedRecvSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "recv", BAD_CAST cbuf);

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsedSendSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "send", BAD_CAST cbuf);

        xmlAddChild(system_node, ip_node);
    }

    /*service ip network*/
    if(0 < schd_cf->sch_service_ip.key.len){
        ip_node = xmlNewNode(NULL, BAD_CAST "serviceip");
        last = ngx_cpymem(cbuf,schd_cf->sch_service_ip.key.data,schd_cf->sch_service_ip.key.len);
        *last = '\0';
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);

        xmlNewProp(ip_node, BAD_CAST "ip", BAD_CAST cbuf);

        if((NULL != schd_cf->sch_service_ip.value.data)&&(0 < schd_cf->sch_service_ip.value.len)) {
            /* set the firewall address */
            last = ngx_cpymem(cbuf,schd_cf->sch_service_ip.value.data,schd_cf->sch_service_ip.value.len);
            *last = '\0';
            xmlNewProp(ip_node, BAD_CAST "nat", BAD_CAST cbuf);
        }

        xmlNewProp(ip_node, BAD_CAST "unit", BAD_CAST "kbps");

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulTotalSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "total", BAD_CAST cbuf);

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsedRecvSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "recv", BAD_CAST cbuf);

        last = ngx_snprintf(cbuf, TRANS_STRING_MAX_LEN, "%d", ulUsedSendSize);
        *last = '\0';
        xmlNewProp(ip_node, BAD_CAST "send", BAD_CAST cbuf);

        xmlAddChild(system_node, ip_node);
    }

    /*disk stat info*/
     if(0 < schd_cf->sch_disk->nelts) {
        disk_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK);
        xmlAddChild(system_node, disk_node);

        kv_diskInfo = schd_cf->sch_disk->elts;
        for(i = 0;i < schd_cf->sch_disk->nelts;i++) {
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            ngx_media_sys_stat_get_diskinfo(cbuf,&ulTotalSize,&ulUsedSize);
            vpath_node = xmlNewNode(NULL, BAD_CAST SYSTEM_NODE_DISK_VPATH);

            last = ngx_cpymem(cbuf,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

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

    return NGX_OK;
}