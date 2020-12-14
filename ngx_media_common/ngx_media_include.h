/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/
#ifndef __H_HTTP_VIDEO_HEAD_H__
#define __H_HTTP_VIDEO_HEAD_H__
#include <net/if.h>       /* for ifconf */
#include <linux/sockios.h>    /* for net status mask */
#include <netinet/in.h>       /* for sockaddr_in */
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
/* libxml2 */
#include "libxml2/libxml/parser.h"

/* http video config file */

#define NGX_VIDEO_TASK      "video_task"
#define NGX_VIDEO_ENDPIONT  "video_endpoint"
#define NGX_TASK_ENDPIONT   "task_endpoint"
#define NGX_TASK_ARGS       "task_args"
#define NGX_TASK_MONITOR    "task_monitor"
#define NGX_TASK_MK_LOG     "task_mk_log"
#define NGX_TASK_MK_RESTART "task_mk_restart"
#define NGX_VIDEO_STAT      "video_stat"
#define NGX_STAT_PASSWD     "manage_password"
#define NGX_TASK_STATIC     "static_task"

#define NGX_TASK_ZMQ_ENDPIONT "tcp://127.0.0.1:1688"


#define NGX_MEDIA_LIVE_CONF_FILE_DEFAULT       "../conf/shmcache.conf"

#define NGX_MEDIA_LIVE                         "media_live"
#define NGX_MEDIA_LIVE_ONPLAY                  "media_live_on_play"
#define NGX_MEDIA_LIVE_ONPLAYING               "media_live_on_playing"
#define NGX_MEDIA_LIVE_ONPLAY_DONE             "media_live_on_play_done"
#define NGX_MEDIA_LIVE_PLAY_TIMEOUT            "media_live_play_timeout"
#define NGX_MEDIA_LIVE_SESSION_CACHE           "media_live_session_cache"



#define NGX_MEDIA_SCH_SERVERID     "sch_server_id"
#define NGX_MEDIA_SCH_SERTYPE      "sch_server_type"
#define NGX_MEDIA_SCH_SIGIP        "sch_signal_ip"
#define NGX_MEDIA_SCH_SERIP        "sch_service_ip"
#define NGX_MEDIA_SCH_DISK         "sch_disk"

#define NGX_MEDIA_SCH_ZK_ADDR      "sch_zk_address"
#define NGX_MEDIA_SCH_ZK_UPDATE    "sch_zk_update"

#define NGX_MEDIA_SCH_ZK_ROOT      "/allmedia"
#define NGX_MEDIA_SCH_ZK_TRANSCODE "/allmedia/transcode"
#define NGX_MEDIA_SCH_ZK_ACCESS    "/allmedia/access"
#define NGX_MEDIA_SCH_ZK_STREAM    "/allmedia/stream"
#define NGX_MEDIA_SCH_ZK_STORAGE   "/allmedia/storage"

#define NGX_ALLMEDIA_TYPE_TRANSCODE   0x01
#define NGX_ALLMEDIA_TYPE_ACCESS      0x02
#define NGX_ALLMEDIA_TYPE_STREAM      0x04
#define NGX_ALLMEDIA_TYPE_STORAGE     0x08
#define NGX_ALLMEDIA_TYPE_ALL         (NGX_ALLMEDIA_TYPE_TRANSCODE|NGX_ALLMEDIA_TYPE_ACCESS|NGX_ALLMEDIA_TYPE_STREAM|NGX_ALLMEDIA_TYPE_STORAGE)

#define NGX_ALLMEDIA_TYPE_MAX         4  


#define NGX_HTTP_FILE_SYS       "file_system"

/* common define */

#define COMMON_XML_REQ          "req"
#define COMMON_XML_REQ_TYPE     "type"
#define COMMON_XML_REQ_STATIC   "static"
#define COMMON_XML_REQ_DYNAMIC  "dynamic"
#define COMMON_XML_RESP         "resp"
#define COMMON_XML_COMMAND      "command"
#define COMMON_XML_NAME         "name"
#define COMMON_XML_UNIT         "unit"
#define COMMON_XML_VALUE        "value"
#define COMMON_XML_SIZE         "size"
#define COMMON_XML_FREE         "free"


#define TASK_STATUS             "status"
#define TASK_COMMAND_INIT       "init"
#define TASK_COMMAND_START      "start"
#define TASK_COMMAND_RUNNING    "running"
#define TASK_COMMAND_STOP       "stop"
#define TASK_COMMAND_BREAK      "break"
#define TASK_COMMAND_CONTROL    "control"

#define TASK_XML_WORKERS        "workers"
#define TASK_XML_WORKER         "worker"
#define TASK_XML_WORKER_ID      "id"
#define TASK_XML_WORKER_MASTER  "master"
#define TASK_XML_WORKER_TYPE    "type"

#define TASK_XML_PARAMS         "params"
#define TASK_XML_PARAM          "param"
#define TASK_XML_PARAM_INCLUDE  "include"

#define TASK_XML_ARGUMENTS      "arguments"
#define TASK_XML_ARGUMENT       "argument"



#define TASK_XML_TRIGGERS       "triggers"
#define TASK_XML_TRIGGER        "trigger"
#define TASK_XML_TRIGGER_AFTER  "after"
#define TASK_XML_TRIGGER_WOKER  "worker"
#define TASK_XML_TRIGGER_DELAY  "delay"

#define TASK_XML_CONTROL_PLAY   "play"
#define TASK_XML_CONTROL_PAUSE  "pause"
#define TASK_XML_CONTROL_SEEK   "seek"
#define TASK_XML_CONTROL_SPEED  "speed"
#define TASK_XML_CONTROL_SCALE  "scale"


#define TASK_XML_STATINFO       "stat_info"
#define TASK_XML_STAT           "stat"





#define SYSTEM_COMMAND_STAT  "stat"
#define SYSTEM_COMMAND_LIST  "list"
#define SYSTEM_COMMAND_DEL   "delete"
#define SYSTEM_COMMAND_MKDIR "mkdir"

#define SYSTEM_NODE_SYSTEM       "system"
#define SYSTEM_NODE_DISK         "disk"
#define SYSTEM_NODE_DISK_VPATH   "vpath"
#define SYSTEM_NODE_DISK_FILE    "file"
#define SYSTEM_NODE_DISK_DIR     "dir"



#define TRANS_REQ_ARG_MANAGE    "manager"

#define TRANS_REQ_ARG_STOP      "stop"
#define TRANS_REQ_ARG_DETAIL    "detail"

#define TRANS_REQ_ARG_TASK      "task"
#define TRANS_REQ_ARG_WORKER    "worker"
#define TRANS_REQ_ARG_TYPE      "type"
#define TRANS_REQ_ARG_PASSWD    "password"







#define TRANS_ERROR_HTML_MSG   "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\"> \
                                </head><body>hi,guy!<br>this is not a web server,You do not have permission to do something<br>\
                                please leave now !thanks<br>---allmedia---</body></html>"
#define TRANS_SUCCESS_HTML_MSG   "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\"> \
                                    </head><body>hi,guy!<br>success!<br>---allmedia---</body></html>"


#define TASK_CHECK_INTERVAL   2000  //2 second
#define TASK_INVALIED_TIME    30    //30 second

#define ZOOKEEPR_UPDATE_TIME  5000  //30 second

#define TRANS_STRING_MAX_LEN  1024
#define TRANS_STRING_4K_LEN   4096

#define TRANS_VPATH_KV_MAX    128
#define TRANS_STRING_IP       128
#define TRANS_VPATH_MAX       1024

#define TASK_ARGS_MAX         256

#define NGX_TASK_ZMQ_ENDPOINT_LEN   64

#define LICENSE_FILE_PATH_LEN          512


enum NGX_MEDIA_ERROR_CODE
{
   NGX_MEDIA_ERROR_CODE_OK               = 0x0000, /*success*/
   NGX_MEDIA_ERROR_CODE_XML_ERROR        = 0x0001, /*xml error*/
   NGX_MEDIA_ERROR_CODE_PARAM_ERROR      = 0x0002, /*param error*/
   NGX_MEDIA_ERROR_CODE_TASK_EXIST       = 0x0003, /*the task is existed */
   NGX_MEDIA_ERROR_CODE_TASK_NO_EXIST    = 0x0004, /*the task is not existed */
   NGX_MEDIA_ERROR_CODE_CREATE_TASK_FAIL = 0x0005, /*creat task fail */
   NGX_MEDIA_ERROR_CODE_RUN_TASK_ERROR   = 0x0006, /*run task fail */
   NGX_MEDIA_ERROR_CODE_SYS_ERROR                  /*System error or unknow error*/
};





typedef struct ngx_media_task_s ngx_media_task_t;

struct ngx_media_task_s {
    ngx_str_t                       task_id;
    volatile ngx_int_t              status;
    volatile time_t                 lastreport;
    time_t                          starttime;
    ngx_msec_t                      mk_restart;
    ngx_int_t                       error_code;
    ngx_int_t                       rep_inter;
    ngx_str_t                       rep_url;
    ngx_url_t                       url;
    ngx_str_t                       xml;

    ngx_pool_t                     *pool;
    ngx_log_t                      *log;

    ngx_resolver_t                 *resolver;
    ngx_msec_t                      resolver_timeout;
    ngx_thread_mutex_t              task_mtx;
    ngx_event_t                     time_event;
    ngx_list_t                     *workers;       /* worker list of the task,(ngx_media_worker_ctx_t)*/
    ngx_media_task_t               *next_task;
    ngx_media_task_t               *prev_task;
};

typedef struct {
    ngx_log_t                      *log;
    ngx_media_task_t               *task_head;
    volatile ngx_uint_t             task_count;
    ngx_thread_mutex_t              task_thread_mtx;
} ngx_media_task_ctx_t;


typedef struct
{
    u_int32_t        bond_mode;
    u_int32_t        num_slaves;
    u_int32_t        miimon;
} ifbond_t;

typedef struct
{
    u_int32_t          slave_id; /* Used as an IN param to the BOND_SLAVE_INFO_QUERY ioctl */
    u_char             slave_name[IFNAMSIZ];
    u_char             link;
    u_char             state;
    u_int32_t          link_failure_count;
} ifslave_t;


typedef struct
{
    u_int32_t  cmd;
    u_int32_t  supported;            /* Features this interface supports */
    u_int32_t  advertising;          /* Features this interface advertises */
    u_int16_t  speed;                /* The forced speed, 10Mb, 100Mb, gigabit */
    uint8_t    duplex;               /* Duplex, half or full */
    uint8_t    port;                 /* Which connector port */
    uint8_t    phy_address;
    uint8_t    transceiver;          /* Which tranceiver to use */
    uint8_t    autoneg;              /* Enable or disable autonegotiation */
    u_int32_t  maxtxpkt;             /* Tx pkts before generating tx int32_t */
    u_int32_t  maxrxpkt;             /* Rx pkts before generating rx int32_t */
    u_int32_t  reserved[4];
} __attribute__ ((packed))          ethtool_cmd_t;
#define ETHTOOL_GSET    0x00000001 /* Get settings. */



static ngx_inline u_char *
ngx_video_strrchr(u_char *begin, u_char *last, u_char c)
{
    while (begin < last) {

        if (*last == c) {
            return last;
        }

        last--;
    }

    return NULL;
}

static ngx_inline u_char *
ngx_media_time2string(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    ngx_localtime(t, &tm);

    return ngx_sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
                       tm.tm_year,
                       tm.tm_mon,
                       tm.tm_mday,
                       tm.tm_hour,
                       tm.tm_min,
                       tm.tm_sec);

}

#endif /*__H_HTTP_VIDEO_HEAD_H__*/
