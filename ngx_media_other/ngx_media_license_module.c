/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_license_module.c
 Version    : V 1.0.0
 Date       : 2017-08-25
 Author     : hexin H.kernel
 Modify     :
            1.2017-08-25: create
******************************************************************************/
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include <ngx_md5.h>

#include <net/if.h>           /* for ifconf */
#include <linux/sockios.h>    /* for net status mask */
#include <netinet/in.h>       /* for sockaddr_in */
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"
#include "libMediaKenerl.h"
#include "mk_def.h"

#define LICENSE_TASK_COUNT_DEFAULT      1   /* default the task count */
#define LICENSE_RTMP_CHANNEL_DEFAULT    2   /* default the RTMP channel */
#define LICENSE_RTSP_CHANNEL_DEFAULT    2   /* default the RTSP channel */
#define LICENSE_HLS_CHANNEL_DEFAULT     2   /* default the HLS channel */


static ngx_str_t    shm_name = ngx_string("license_info");

typedef struct {
    u_char                         license_file[LICENSE_FILE_PATH_LEN];
    ngx_uint_t                     task_max;
    ngx_uint_t                     task_count;
    ngx_uint_t                     rtmp_channel;
    ngx_uint_t                     rtmp_count;
    ngx_uint_t                     hls_channel;
    ngx_uint_t                     hls_count;
    ngx_uint_t                     rtsp_channel;
    ngx_uint_t                     rtsp_count;
} ngx_license_info_t;

typedef struct {
    ngx_str_t                      license_file;
    ngx_shm_zone_t                *shm_zone;
} ngx_license_conf_t;

static ngx_license_conf_t *g_license_conf = NULL;

static void     *ngx_license_create_conf(ngx_cycle_t *cycle);
static char     *ngx_license_init_conf(ngx_cycle_t *cycle, void *conf);
static char     *ngx_license_file_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_license_init_process(ngx_cycle_t *cycle);
static void      ngx_license_exit_process(ngx_cycle_t *cycle);
static ngx_int_t ngx_license_shm_init(ngx_shm_zone_t *shm_zone, void *data);




static ngx_command_t  ngx_license_commands[] = {
    { ngx_string("license"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_license_file_conf,
      0,
      offsetof(ngx_license_conf_t, license_file),
      NULL },

    ngx_null_command
};


static ngx_core_module_t  ngx_license_module_ctx = {
    ngx_string("license"),
    ngx_license_create_conf,
    ngx_license_init_conf
};



ngx_module_t  ngx_license_module = {
    NGX_MODULE_V1,
    &ngx_license_module_ctx,                  /* module context */
    ngx_license_commands,                     /* module directives */
    NGX_CORE_MODULE,                          /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    ngx_license_init_process,                 /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    ngx_license_exit_process,                 /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};

static void*
ngx_license_create_conf(ngx_cycle_t *cycle)
{
    ngx_license_conf_t  *lcf;

    lcf = ngx_pcalloc(cycle->pool, sizeof(ngx_license_conf_t));
    if (lcf == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_license_module: ngx_pcalloc conf fail.");
        return NULL;
    }

    lcf->shm_zone  = NGX_CONF_UNSET_PTR;
    g_license_conf = lcf;
    return lcf;
}
static char*
ngx_license_init_conf(ngx_cycle_t *cycle, void *conf)
{
    //ngx_license_conf_t  *lcf = conf;
    //lcf->shm_zone = NGX_CONF_UNSET_PTR;
    //ngx_str_null(&lcf->license_file);
    return NGX_CONF_OK;
}
static char*
ngx_license_file_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_license_conf_t  *lcf;
    lcf = (ngx_license_conf_t*)ngx_license_get_conf(cf,ngx_license_module);
    lcf->shm_zone = ngx_shared_memory_add(cf, &shm_name, ngx_pagesize * 2,
                                           &ngx_license_module);
    if (lcf->shm_zone == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, cf->log, 0, "ngx_license_module: ngx_shared_memory_add fail.");
        return NGX_CONF_ERROR;
    }

    lcf->shm_zone->init = ngx_license_shm_init;

    return ngx_conf_set_str_slot(cf,cmd,conf);
}


static void
ngx_media_license_init(ngx_cycle_t *cycle,ngx_license_conf_t* lcf)
{
    
    ngx_uint_t                     task_count   = LICENSE_TASK_COUNT_DEFAULT;
    ngx_uint_t                     rtmp_channel = LICENSE_RTMP_CHANNEL_DEFAULT;
    ngx_uint_t                     rtsp_channel = LICENSE_RTSP_CHANNEL_DEFAULT;
    ngx_uint_t                     hls_channel  = LICENSE_HLS_CHANNEL_DEFAULT;
    u_char                         licfile[LICENSE_FILE_PATH_LEN];
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    ngx_memzero(licfile, LICENSE_FILE_PATH_LEN);

    u_char* last = ngx_snprintf(licfile, LICENSE_FILE_PATH_LEN,"%V/%V",&cycle->conf_prefix,&lcf->license_file);
    *last = '\0';

    shm_zone = lcf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    license->task_max     = LICENSE_TASK_COUNT_DEFAULT;
    license->task_count   = 0;
    license->rtmp_channel = LICENSE_RTMP_CHANNEL_DEFAULT;
    license->rtmp_count   = 0;
    license->hls_channel  = LICENSE_HLS_CHANNEL_DEFAULT;
    license->hls_count    = 0;
    license->rtsp_channel = LICENSE_RTSP_CHANNEL_DEFAULT;
    license->rtsp_count   = 0;    
    ngx_shmtx_unlock(&shpool->mutex);
    
    if(MK_ERROR_CODE_OK != mk_lib_init((const char*)&licfile[0],1)) {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"init the mediakernel lib fail.");
        return;
    }

    task_count = mk_get_ability("task");
    rtmp_channel = mk_get_ability("rtmp");
    hls_channel = mk_get_ability("http");
    rtsp_channel = mk_get_ability("rtsp");  

    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,"******license info,task:[%d] rtmp:[%d] http:[%d] rtsp:[%d]****.",
                           task_count,rtmp_channel,hls_channel,rtsp_channel);  

    ngx_shmtx_lock(&shpool->mutex);
    license->task_max     = task_count;
    license->rtmp_channel = rtmp_channel;
    license->hls_channel  = hls_channel;
    license->rtsp_channel = rtsp_channel;   
    ngx_shmtx_unlock(&shpool->mutex);

    return;
}

static ngx_int_t
ngx_license_init_process(ngx_cycle_t *cycle)
{
    ngx_license_conf_t         *lcf;
    lcf = (ngx_license_conf_t*)ngx_license_get_cycle_conf(cycle,ngx_license_module);
    if(NULL == lcf) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_license_module: get cycle conf fail.");
        return NGX_ERROR;
    }
    if (lcf->shm_zone == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_license_module: the share memory is null.");
        return NGX_ERROR;
    }
    /* execs are always started by the first worker */
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_license_module,the process:[%d] is not 0,no need start.",ngx_process_slot);
        return NGX_OK;
    }

    ngx_media_license_init(cycle,lcf);
    return NGX_OK;
}
static void      
ngx_license_exit_process(ngx_cycle_t *cycle)
{
    return ;
}

static ngx_int_t
ngx_license_shm_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t    *shpool;
    ngx_license_info_t *license;

    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    license = ngx_slab_alloc(shpool, sizeof(ngx_license_info_t));
    if (license == NULL) {
        return NGX_ERROR;
    }
    shm_zone->data = license;

    return NGX_OK;
}
ngx_uint_t 
ngx_media_license_task_max()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     max;

    if(NULL == g_license_conf) {
        return LICENSE_TASK_COUNT_DEFAULT;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    max = license->task_max;    
    ngx_shmtx_unlock(&shpool->mutex);
    return max;
}
void      
ngx_media_license_task_inc()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    ++license->task_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
void       
ngx_media_license_task_dec()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    --license->task_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
ngx_uint_t 
ngx_media_license_task_current()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     count;

    if(NULL == g_license_conf) {
        return 0;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    count = license->task_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return count;
}
ngx_uint_t
ngx_media_license_rtmp_channle()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     max;

    if(NULL == g_license_conf) {
        return LICENSE_RTMP_CHANNEL_DEFAULT;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    max = license->rtmp_channel;    
    ngx_shmtx_unlock(&shpool->mutex);
    return max;
}

void       
ngx_media_license_rtmp_inc()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    ++license->rtmp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
void       
ngx_media_license_rtmp_dec()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    --license->rtmp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
ngx_uint_t
ngx_media_license_rtmp_current()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     count;

    if(NULL == g_license_conf) {
        return 0;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    count = license->rtmp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return count;
}


ngx_uint_t
ngx_media_license_rtsp_channle()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     max;

    if(NULL == g_license_conf) {
        return LICENSE_RTSP_CHANNEL_DEFAULT;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    max = license->rtsp_channel;    
    ngx_shmtx_unlock(&shpool->mutex);
    return max;
}

void       
ngx_media_license_rtsp_inc()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    ++license->rtsp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
void       
ngx_media_license_rtsp_dec()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    --license->rtsp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
ngx_uint_t
ngx_media_license_rtsp_current()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     count;

    if(NULL == g_license_conf) {
        return 0;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    count = license->rtsp_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return count;
}

ngx_uint_t
ngx_media_license_hls_channle()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     max;

    if(NULL == g_license_conf) {
        return LICENSE_HLS_CHANNEL_DEFAULT;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    max = license->hls_channel;    
    ngx_shmtx_unlock(&shpool->mutex);
    return max;
}
void       
ngx_media_license_hls_inc()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    ++license->hls_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}
void       
ngx_media_license_hls_dec()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;

    if(NULL == g_license_conf) {
        return;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    --license->hls_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return;
}

ngx_uint_t
ngx_media_license_hls_current()
{
    ngx_slab_pool_t               *shpool;
    ngx_shm_zone_t                *shm_zone;
    ngx_license_info_t            *license;
    ngx_uint_t                     count;

    if(NULL == g_license_conf) {
        return 0;
    }

    shm_zone = g_license_conf->shm_zone;
    shpool   = (ngx_slab_pool_t *) shm_zone->shm.addr;
    license  = shm_zone->data;
    
    ngx_shmtx_lock(&shpool->mutex);
    count = license->hls_count;    
    ngx_shmtx_unlock(&shpool->mutex);
    return count;
}

ngx_str_t* 
ngx_media_license_file_path()
{
    if(NULL == g_license_conf) {
        return NULL;
    }
    return &g_license_conf->license_file;
}