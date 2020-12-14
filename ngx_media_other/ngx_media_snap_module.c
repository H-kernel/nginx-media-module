/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_snap_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
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
#include <sys/statvfs.h>
#include "libMediaKenerl.h"
#include "mk_def.h"
#include "ngx_media_license_module.h"


#define REQ_ARG_WIDTH        "width"
#define REQ_ARG_HEIGTH       "height"
#define REQ_ARG_OFFSET       "offset"
#define REQ_ARG_DURATION     "duration"
#define REQ_ARG_FPS          "fps"
#define REQ_ARG_VPATH        "vpath"
#define REQ_ARG_VFILE        "vfile"
#define REQ_ARG_DOWNLOAD     "dl"
#define REQ_ARG_SCALE        "scale"
#define REQ_ARG_SPEED        "speed"
#define REQ_ARG_FILTER       "filter"


#define SUFFIXES_JPEG        ".jpeg"
#define SUFFIXES_JPG         ".jpg"
#define SUFFIXES_PNG         ".png"
#define SUFFIXES_GIF         ".gif"
#define SUFFIXES_BMP         ".bmp"
#define SUFFIXES_MP4         ".mp4"


#define DOWNLODA_FROM_ST     "st"
#define DOWNLODA_FROM_VCN    "vcn"


#define SNAP_WIDTH_DEFAULT    1280
#define SNAP_HEIGTH_DEFAULT   720
#define SNAP_OFFSET_DEFAULT   0
#define SNAP_DURATION_DEFAULT 10
#define SNAP_FPS_DEFAULT      8

#define SNAP_DURATION_MAX     300
#define MP4_DURATION_MAX      600





#define SNAP_MK_PARAM_MAX     32



static char* ngx_media_snap_name = "ngx_media_snap";

typedef struct {
    ngx_str_t                      source_dir;
    ngx_int_t                      snap_width;
    ngx_int_t                      snap_height;
    ngx_int_t                      snap_offset;
    ngx_int_t                      snap_duration;
    ngx_int_t                      snap_fps;
} ngx_media_snap_loc_conf_t;


static char*     ngx_media_snap_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*     ngx_media_snap_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_media_snap_init_worker(ngx_cycle_t *cycle);
static void      ngx_media_snap_exit_worker(ngx_cycle_t *cycle);
/*static void      ngx_media_snap_check_task(ngx_event_t *ev);*/



static ngx_command_t  ngx_media_snap_commands[] = {

    { ngx_string("video_snap"),
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      ngx_media_snap_init,
      0,
      0,
      NULL },

    { ngx_string("snap_source_dir"),
      NGX_HTTP_LOC_CONF |NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, source_dir),
      NULL },

    { ngx_string("snap_width"),
      NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, snap_width),
      NULL },

    { ngx_string("snap_height"),
      NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, snap_height),
      NULL },

    { ngx_string("snap_offset"),
      NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, snap_offset),
      NULL },

    { ngx_string("snap_duration"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, snap_duration),
      NULL },


    { ngx_string("snap_fps"),
      NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_snap_loc_conf_t, snap_fps),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_snap_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_media_snap_create_loc_conf,    /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_snap_module = {
    NGX_MODULE_V1,
    &ngx_media_snap_module_ctx,        /* module context */
    ngx_media_snap_commands,           /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_media_snap_init_worker,        /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    ngx_media_snap_exit_worker,        /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

static void
ngx_media_snap_image(ngx_http_request_t *r,ngx_str_t *vpath,ngx_str_t* image,
                    ngx_int_t width,ngx_int_t heigth,ngx_uint_t offset,ngx_str_t* filter)
{

    char       *paramlist[SNAP_MK_PARAM_MAX];
    ngx_int_t   paramcount = 0;
    MK_HANDLE   handle     = NULL;
    u_char     *last       = NULL;

    u_char      video_file[TRANS_VPATH_MAX];
    u_char      image_file[TRANS_VPATH_MAX];
    u_char      str_offset[8];
    u_char      str_format[32];
    u_char      image_filter[TRANS_VPATH_MAX];

    ngx_memzero(video_file, TRANS_VPATH_MAX);
    ngx_memzero(image_file, TRANS_VPATH_MAX);
    ngx_memzero(str_offset, 8);
    ngx_memzero(str_format, 32);
    ngx_memzero(image_filter, TRANS_VPATH_MAX);

    last = ngx_snprintf(video_file,TRANS_VPATH_MAX,"%V",vpath);
    *last = '\0';
    last = ngx_snprintf(image_file,TRANS_VPATH_MAX,"%V",image);
    *last = '\0';
    last = ngx_snprintf(str_offset,8,"%i",offset);
    *last = '\0';
    last = ngx_snprintf(str_format,32,"%ix%i",width,heigth);
    *last = '\0';

    /* create the mk handle to capture the image */
    handle = mk_create_handle(ngx_media_snap_name,MK_HANDLE_TYPE_TASK);
    if(NULL == handle) {
        return;
    }
    
    if(0 < offset) {
        paramlist[paramcount] = "-ss";                    paramcount++;
        paramlist[paramcount] = (char*)&str_offset[0];    paramcount++;
    }

    if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "rtsp://", 7)) {
        paramlist[paramcount] = "-reorder_queue_size";   paramcount++;
        paramlist[paramcount] = "2000";                  paramcount++;

        paramlist[paramcount] = "-buffer_size";          paramcount++;
        paramlist[paramcount] = "4096000";               paramcount++;

        paramlist[paramcount] = "-stimeout";             paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;

        paramlist[paramcount] = "-rtsp_transport";       paramcount++;
        paramlist[paramcount] = "tcp";                   paramcount++;

        paramlist[paramcount] = "-skip_frame";           paramcount++;
        paramlist[paramcount] = "nointra";               paramcount++;
    }
    else if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "http://", 7)) {
        paramlist[paramcount] = "-timeout";              paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;
    }

    paramlist[paramcount] = "-src";                       paramcount++;
    paramlist[paramcount] = (char*)&video_file[0];        paramcount++;

    paramlist[paramcount] = "-f";                         paramcount++;
    paramlist[paramcount] = "image2";                     paramcount++;

    if((-1 != width)&&(-1 != heigth)){

        paramlist[paramcount] = "-s";                     paramcount++;
        paramlist[paramcount] = (char*)&str_format[0];    paramcount++;
    }

    if((0 < filter->len)&&(NULL != filter->data)) {
        last = ngx_snprintf(image_filter,TRANS_VPATH_MAX,"%V",filter);
        *last = '\0';
        paramlist[paramcount] = "-filter_complex";       paramcount++;
        paramlist[paramcount] = (char*)&image_filter[0]; paramcount++;
    }

    paramlist[paramcount] = "-vframes";                   paramcount++;
    paramlist[paramcount] = "1";                          paramcount++;

    paramlist[paramcount] = "-dst";                       paramcount++;
    paramlist[paramcount] = (char*)&image_file[0];        paramcount++;

    int32_t ret = mk_do_task(handle, paramcount,(char**)paramlist);

    if(MK_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log,0,
                          "ngx_media_snap_image,snap image:[%V] from video:[%V] fail.",vpath,image);
    }

    /* destory the mk handle */
    if(NULL != handle) {
        mk_destory_handle(handle);
        handle = NULL;
    }

    return;
}

static void
ngx_media_snap_gif(ngx_http_request_t *r,ngx_str_t *vpath,ngx_str_t* image,
                          ngx_int_t width,ngx_int_t heigth,ngx_uint_t offset,
                          ngx_uint_t duration,ngx_uint_t fps,
                          ngx_str_t* scale,ngx_str_t* speed,ngx_str_t* filter)
{

    char       *paramlist[SNAP_MK_PARAM_MAX];
    ngx_int_t   paramcount = 0;
    MK_HANDLE   handle     = NULL;
    u_char     *last       = NULL;

    u_char      video_file[TRANS_VPATH_MAX];
    u_char      image_file[TRANS_VPATH_MAX];
    u_char      str_offset[8];
    u_char      str_duration[8];
    u_char      str_fps[8];
    u_char      str_format[32];
    u_char      str_scale[64];
    u_char      str_speed[64];
    u_char      image_filter[TRANS_VPATH_MAX];

    ngx_memzero(video_file, TRANS_VPATH_MAX);
    ngx_memzero(image_file, TRANS_VPATH_MAX);
    ngx_memzero(str_offset, 8);
    ngx_memzero(str_duration, 8);
    ngx_memzero(str_fps, 8);
    ngx_memzero(str_format, 32);
    ngx_memzero(str_scale, 64);
    ngx_memzero(str_speed, 64);
    ngx_memzero(image_filter, TRANS_VPATH_MAX);

    ngx_uint_t giftime = duration;

    if(giftime > SNAP_DURATION_MAX) {
        giftime = SNAP_DURATION_MAX;
    }

    last = ngx_snprintf(video_file,TRANS_VPATH_MAX,"%V",vpath);
    *last = '\0';
    last = ngx_snprintf(image_file,TRANS_VPATH_MAX,"%V",image);
    *last = '\0';
    last = ngx_snprintf(str_offset,8,"%i",offset);
    *last = '\0';
    last = ngx_snprintf(str_duration,8,"%i",giftime);
    *last = '\0';
    last = ngx_snprintf(str_fps,8,"%i",fps);
    *last = '\0';
    last = ngx_snprintf(str_format,8,"%ix%i",width,heigth);
    *last = '\0';
    if(0 < scale->len) {
        last = ngx_snprintf(str_scale,64,"%V",scale);
        *last = '\0';
    }
    if(0 < speed->len) {
        last = ngx_snprintf(str_speed,64,"%V",speed);
        *last = '\0';
    }

    /* create the mk handle to capture the image */
    handle = mk_create_handle(ngx_media_snap_name,MK_HANDLE_TYPE_TASK);
    if(NULL == handle) {
        return;
    }

    if(0 < offset) {
        paramlist[paramcount] = "-ss";                   paramcount++;
        paramlist[paramcount] = (char*)&str_offset[0];   paramcount++;
    }

    if(0 < scale->len) {
        paramlist[paramcount] = "-scale";                paramcount++;
        paramlist[paramcount] = (char*)&str_scale[0];    paramcount++;
    }
    if(0 < speed->len) {
        paramlist[paramcount] = "-speed";                paramcount++;
        paramlist[paramcount] = (char*)&str_speed[0];    paramcount++;
    }
    
    if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "rtsp://", 7)) {
        paramlist[paramcount] = "-reorder_queue_size";   paramcount++;
        paramlist[paramcount] = "1000";                  paramcount++;

        paramlist[paramcount] = "-buffer_size";          paramcount++;
        paramlist[paramcount] = "2048000";               paramcount++;

        paramlist[paramcount] = "-stimeout";             paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;

        paramlist[paramcount] = "-rtsp_transport";       paramcount++;
        paramlist[paramcount] = "tcp";                   paramcount++;

        paramlist[paramcount] = "-skip_frame";           paramcount++;
        paramlist[paramcount] = "noref";                 paramcount++;
    }
    else if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "http://", 7)) {
        paramlist[paramcount] = "-timeout";              paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;
    }

    paramlist[paramcount] = "-src";                      paramcount++;
    paramlist[paramcount] = (char*)&video_file[0];       paramcount++;

    if((-1 != width)&&(-1 != heigth)){

        paramlist[paramcount] = "-s";                     paramcount++;
        paramlist[paramcount] = (char*)&str_format[0];    paramcount++;
    }

    if((0 < filter->len)&&(NULL != filter->data)) {
        last = ngx_snprintf(image_filter,TRANS_VPATH_MAX,"%V",filter);
        *last = '\0';
        paramlist[paramcount] = "-filter_complex";       paramcount++;
        paramlist[paramcount] = (char*)&image_filter[0]; paramcount++;
    }

    paramlist[paramcount] = "-t";                        paramcount++;
    paramlist[paramcount] = (char*)&str_duration[0];     paramcount++;

    paramlist[paramcount] = "-r";                        paramcount++;
    paramlist[paramcount] = (char*)&str_fps[0];          paramcount++;

    paramlist[paramcount] = "-dst";                      paramcount++;
    paramlist[paramcount] = (char*)&image_file[0];       paramcount++;

    int32_t ret = mk_do_task(handle, paramcount,(char**)paramlist);

    if(MK_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log,0,
                          "ngx_media_snap_gif,snap gif:[%V] from video:[%V] fail.",vpath,image);
    }

    /* destory the mk handle */
    if(NULL != handle) {
        mk_destory_handle(handle);
        handle = NULL;
    }

    return;
}
static void
ngx_media_snap_mp4(ngx_http_request_t *r,ngx_str_t *vpath,ngx_str_t* mp4,
                          ngx_int_t width,ngx_int_t heigth,ngx_uint_t offset,
                          ngx_uint_t duration,ngx_uint_t fps,
                          ngx_str_t* scale,ngx_str_t* speed,ngx_str_t* filter)
{

    char       *paramlist[SNAP_MK_PARAM_MAX];
    ngx_int_t   paramcount = 0;
    MK_HANDLE   handle     = NULL;
    u_char     *last       = NULL;

    u_char      video_file[TRANS_VPATH_MAX];
    u_char      mp4_file[TRANS_VPATH_MAX];
    u_char      str_offset[8];
    u_char      str_duration[8];
    u_char      str_fps[8];
    u_char      str_format[32];
    u_char      str_scale[64];
    u_char      str_speed[64];
    u_char      image_filter[TRANS_VPATH_MAX];

    ngx_memzero(video_file, TRANS_VPATH_MAX);
    ngx_memzero(mp4_file, TRANS_VPATH_MAX);
    ngx_memzero(str_offset, 8);
    ngx_memzero(str_duration, 8);
    ngx_memzero(str_fps, 8);
    ngx_memzero(str_format, 32);
    ngx_memzero(str_scale, 64);
    ngx_memzero(str_speed, 64);
    ngx_memzero(image_filter, TRANS_VPATH_MAX);

    ngx_uint_t mp4time = duration;

    if(mp4time > MP4_DURATION_MAX) {
        mp4time = MP4_DURATION_MAX;
    }

    last = ngx_snprintf(video_file,TRANS_VPATH_MAX,"%V",vpath);
    *last = '\0';
    last = ngx_snprintf(mp4_file,TRANS_VPATH_MAX,"%V",mp4);
    *last = '\0';
    last = ngx_snprintf(str_offset,8,"%i",offset);
    *last = '\0';
    last = ngx_snprintf(str_duration,8,"%i",mp4time);
    *last = '\0';
    last = ngx_snprintf(str_fps,8,"%i",fps);
    *last = '\0';
    last = ngx_snprintf(str_format,8,"%ix%i",width,heigth);
    *last = '\0';
    if(0 < scale->len) {
        last = ngx_snprintf(str_scale,64,"%V",scale);
        *last = '\0';
    }
    if(0 < speed->len) {
        last = ngx_snprintf(str_speed,64,"%V",speed);
        *last = '\0';
    }
    /* create the mk handle to capture the image */
    handle = mk_create_handle(ngx_media_snap_name,MK_HANDLE_TYPE_TASK);
    if(NULL == handle) {
        return;
    }


    if(0 < offset) {
        paramlist[paramcount] = "-ss";                   paramcount++;
        paramlist[paramcount] = (char*)&str_offset[0];   paramcount++;
    }

    if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "rtsp://", 7)) {
        paramlist[paramcount] = "-reorder_queue_size";   paramcount++;
        paramlist[paramcount] = "1000";                  paramcount++;

        paramlist[paramcount] = "-buffer_size";          paramcount++;
        paramlist[paramcount] = "2048000";               paramcount++;

        paramlist[paramcount] = "-stimeout";             paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;

        paramlist[paramcount] = "-rtsp_transport";       paramcount++;
        paramlist[paramcount] = "tcp";                   paramcount++;

        paramlist[paramcount] = "-skip_frame";           paramcount++;
        paramlist[paramcount] = "noref";                 paramcount++;
    }
    else if (0 == ngx_strncasecmp(&video_file[0], (u_char *) "http://", 7)) {
        paramlist[paramcount] = "-timeout";              paramcount++;
        paramlist[paramcount] = "5000000";               paramcount++;
    }

    if(0 < scale->len) {
        paramlist[paramcount] = "-scale";                paramcount++;
        paramlist[paramcount] = (char*)&str_scale[0];    paramcount++;
    }
    if(0 < speed->len) {
        paramlist[paramcount] = "-speed";                paramcount++;
        paramlist[paramcount] = (char*)&str_speed[0];    paramcount++;
    }

    //paramlist[paramcount] = "-t";                        paramcount++;
    //paramlist[paramcount] = (char*)&str_duration[0];     paramcount++;

    paramlist[paramcount] = "-src";                      paramcount++;
    paramlist[paramcount] = (char*)&video_file[0];       paramcount++;


    if((-1 == width)
        ||(-1 == heigth)){        
        paramlist[paramcount] = "-vcodec";               paramcount++;
        paramlist[paramcount] = "libx264";               paramcount++; 
        paramlist[paramcount] = "-copy_if_h264";         paramcount++;/* must behide vcodec*/       
    }
    else {
        paramlist[paramcount] = "-s";                    paramcount++;
        paramlist[paramcount] = (char*)&str_format[0];   paramcount++;

        paramlist[paramcount] = "-vcodec";               paramcount++;
        paramlist[paramcount] = "libx264";               paramcount++;
    }

    if((0 < filter->len)&&(NULL != filter->data)) {
        last = ngx_snprintf(image_filter,TRANS_VPATH_MAX,"%V",filter);
        *last = '\0';
        paramlist[paramcount] = "-filter_complex";       paramcount++;
        paramlist[paramcount] = (char*)&image_filter[0]; paramcount++;
    }
    
    paramlist[paramcount] = "-max_muxing_queue_size";    paramcount++;
    paramlist[paramcount] = "256";                       paramcount++;

    //paramlist[paramcount] = "-acodec";                   paramcount++;
    //paramlist[paramcount] = "aac";                       paramcount++;
    paramlist[paramcount] = "-an";                       paramcount++;

    paramlist[paramcount] = "-f";                        paramcount++;
    paramlist[paramcount] = "mp4";                       paramcount++;
    
    paramlist[paramcount] = "-t";                        paramcount++;
    paramlist[paramcount] = (char*)&str_duration[0];     paramcount++;

    paramlist[paramcount] = "-dst";                      paramcount++;
    paramlist[paramcount] = (char*)&mp4_file[0];         paramcount++;


    int32_t ret = mk_do_task(handle, paramcount,(char**)paramlist);

    if(MK_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log,0,
                          "ngx_media_snap_mp4,snap mp4:[%V] from video:[%V] fail.",vpath,mp4);
    }

    /* destory the mk handle */
    if(NULL != handle) {
        mk_destory_handle(handle);
        handle = NULL;
    }

    return;
}

static void
ngx_media_snap_from_vcn(ngx_http_request_t *r,ngx_str_t *vpath,ngx_str_t* image,ngx_str_t* filter)
{

    char       *paramlist[SNAP_MK_PARAM_MAX];
    ngx_int_t   paramcount = 0;
    MK_HANDLE   handle     = NULL;
    u_char     *last       = NULL;

    u_char      video_file[TRANS_VPATH_MAX];
    u_char      image_file[TRANS_VPATH_MAX];
    u_char      image_filter[TRANS_VPATH_MAX];

    ngx_memzero(video_file, TRANS_VPATH_MAX);
    ngx_memzero(image_file, TRANS_VPATH_MAX);
    ngx_memzero(image_filter, TRANS_VPATH_MAX);

    last = ngx_snprintf(video_file,TRANS_VPATH_MAX,"%V",vpath);
    *last = '\0';
    last = ngx_snprintf(image_file,TRANS_VPATH_MAX,"%V",image);
    *last = '\0';

    /* create the mk handle to capture the image */
    handle = mk_create_handle(ngx_media_snap_name,MK_HANDLE_TYPE_TASK);
    if(NULL == handle) {
        return;
    }


    paramlist[paramcount] = "-src";                paramcount++;
    paramlist[paramcount] = (char*)&video_file[0]; paramcount++;

    if((0 < filter->len)&&(NULL != filter->data)) {
        last = ngx_snprintf(image_filter,TRANS_VPATH_MAX,"%V",filter);
        *last = '\0';
        paramlist[paramcount] = "-filter_complex";       paramcount++;
        paramlist[paramcount] = (char*)&image_filter[0]; paramcount++;
    }

    paramlist[paramcount] = "-dst";                paramcount++;
    paramlist[paramcount] = (char*)&image_file[0]; paramcount++;

    paramcount = 4;
    

    int32_t ret = mk_do_task(handle, paramcount,(char**)paramlist);

    if(MK_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log,0,
                          "ngx_media_snap_image,download vcn image:[%V] from video:[%V] fail.",vpath,image);
    }

    /* destory the mk handle */
    if(NULL != handle) {
        mk_destory_handle(handle);
        handle = NULL;
    }

    return;
}


static ngx_int_t
ngx_media_snap_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    ngx_media_snap_loc_conf_t     *video_conf;
    ngx_file_info_t                fi;
    u_char                        *last;
    size_t                         root;
    ngx_str_t                      reqfile;

    ngx_str_t                      arg;
    ngx_str_t                      strinput;
    ngx_str_t                      strvpath;
    ngx_str_t                      strvfile;
    ngx_str_t                      strdownload;
    ngx_str_t                      strScale;
    ngx_str_t                      strSpeed;
    ngx_str_t                      strFilter;

    ngx_int_t                      width;
    ngx_int_t                      heigth;
    ngx_uint_t                     offset;
    ngx_uint_t                     duration;
    ngx_uint_t                     fps;

    ngx_int_t                      video_copy = 1;

    ngx_str_null(&reqfile);
    ngx_str_null(&strinput);
    ngx_str_null(&strvpath);
    ngx_str_null(&strvfile);
    ngx_str_null(&strdownload);
    ngx_str_null(&strScale);
    ngx_str_null(&strSpeed);
    ngx_str_null(&strFilter);
    ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido handle snap request.");


    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx http vido snap request method is invalid.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx http vido snap request uri is invalid.");
        return NGX_DECLINED;
    }

    /* discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    video_conf = ngx_http_get_module_loc_conf(r, ngx_media_snap_module);

    if((video_conf->snap_width == NGX_CONF_UNSET)
        ||(video_conf->snap_height == NGX_CONF_UNSET)) {
        width  = SNAP_WIDTH_DEFAULT;
        heigth = SNAP_HEIGTH_DEFAULT;
    }
    else {
        width  = video_conf->snap_width;
        heigth = video_conf->snap_height;
        video_copy = 0;
    }

    if(video_conf->snap_offset == NGX_CONF_UNSET) {
        offset  = SNAP_OFFSET_DEFAULT;
    }
    else {
        offset  = video_conf->snap_offset;
    }

    if(video_conf->snap_duration == NGX_CONF_UNSET) {
        duration  = SNAP_DURATION_DEFAULT;
    }
    else {
        duration  = video_conf->snap_duration;
    }

    if(video_conf->snap_fps== NGX_CONF_UNSET) {
        fps  = SNAP_FPS_DEFAULT;
    }
    else {
        fps  = video_conf->snap_fps;
    }

    /* 1. check the file exist */
    last = ngx_http_map_uri_to_path(r, &reqfile, &root, 0);
    if (NULL == last)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the reuquest file path is not exist.");
        return NGX_HTTP_NOT_FOUND;
    }

    reqfile.len = last - reqfile.data;

    rc = ngx_file_info(reqfile.data, &fi);
    if (rc != NGX_FILE_ERROR)
    {
        rc = ngx_is_file(&fi);
        if(rc)
        {
            /* the file exist, so deal will the next location */
            return NGX_DECLINED;
        }
    }

    /* 2.parse the input args */
    if (r->args.len) {
        if (ngx_http_arg(r, (u_char *) REQ_ARG_WIDTH, ngx_strlen(REQ_ARG_WIDTH), &arg) == NGX_OK) {
            width = ngx_atoi(arg.data, arg.len);
            video_copy = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_HEIGTH, ngx_strlen(REQ_ARG_HEIGTH), &arg) == NGX_OK) {
            heigth = ngx_atoi(arg.data, arg.len);
            video_copy = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_VPATH, ngx_strlen(REQ_ARG_VPATH), &strvpath) != NGX_OK) {
            strvpath.len = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_VFILE, ngx_strlen(REQ_ARG_VFILE), &strvfile) != NGX_OK) {
            strvfile.len = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_DOWNLOAD, ngx_strlen(REQ_ARG_DOWNLOAD), &strdownload) != NGX_OK) {
            strdownload.len = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_OFFSET, ngx_strlen(REQ_ARG_OFFSET), &arg) == NGX_OK) {
             offset = ngx_atoi(arg.data, arg.len);
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_DURATION, ngx_strlen(REQ_ARG_DURATION), &arg) == NGX_OK) {
             duration= ngx_atoi(arg.data, arg.len);
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_FPS, ngx_strlen(REQ_ARG_FPS), &arg) == NGX_OK) {
             fps= ngx_atoi(arg.data, arg.len);
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_FPS, ngx_strlen(REQ_ARG_FPS), &arg) == NGX_OK) {
             fps= ngx_atoi(arg.data, arg.len);
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_SCALE, ngx_strlen(REQ_ARG_SCALE), &strScale) != NGX_OK) {
             strScale.len = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_SPEED, ngx_strlen(REQ_ARG_SPEED), &strSpeed) != NGX_OK) {
             strSpeed.len = 0;
        }
        if (ngx_http_arg(r, (u_char *) REQ_ARG_FILTER, ngx_strlen(REQ_ARG_FILTER), &strFilter) != NGX_OK) {
             strFilter.len = 0;
        }
    }

    strinput.len = 0;

    if((0 < strvfile.len) &&(NULL != strvfile.data)) {
        if((0 < strvpath.len) &&(NULL != strvpath.data)) {
            if(NULL == ngx_media_sys_map_vpath_vfile_to_path(r,&strvpath,&strvfile,&strinput)) {
                return NGX_ERROR;
            }
        }
        else if((0 < video_conf->source_dir.len) &&(NULL != video_conf->source_dir.data)){
            strinput.len = video_conf->source_dir.len + strvfile.len + 2;
            strinput.data = ngx_pcalloc(r->pool,strinput.len);
            if('/' == video_conf->source_dir.data[video_conf->source_dir.len - 1]) {
                last = ngx_snprintf(strinput.data,strinput.len,"%V%V",&video_conf->source_dir,&strvfile);
            }
            else {
                last = ngx_snprintf(strinput.data,strinput.len,"%V/%V",&video_conf->source_dir,&strvfile);
            }
            *last = '\0';
        }
        else {
            return NGX_ERROR;
        }
    }
    else if((0 < strvpath.len) &&(NULL != strvpath.data)) {
        u_char* unescape = ngx_pcalloc(r->pool,strvpath.len+1);
        u_char* pszDst = unescape;
        ngx_unescape_uri(&pszDst,&strvpath.data, strvpath.len, 0);
        pszDst = '\0';

        strinput.data = unescape;
        strinput.len  = strvpath.len + 1;
    }
    else {
        return NGX_ERROR;
    }

    if((0 < strFilter.len) &&(NULL != strFilter.data)) {
        u_char* unescape = ngx_pcalloc(r->pool,strFilter.len+1);
        u_char* pszDst = unescape;
        ngx_unescape_uri(&pszDst,&strFilter.data, strFilter.len, 0);
        pszDst = '\0';

        strFilter.data = unescape;
        strFilter.len  = strFilter.len + 1;
    }

    /*  remove the args from the uri */
    if (r->args_start)
    {
        r->uri.len = r->args_start - 1 - r->uri_start;
        r->uri.data[r->uri.len] ='\0';
    }

    if(0 == strinput.len)
    {
        return NGX_ERROR;
    }

    u_char *unescape = ngx_pcalloc(r->pool,strinput.len+1);
    u_char *pszDst = unescape;
    ngx_unescape_uri(&pszDst,&strinput.data, strinput.len, 0);
    pszDst = '\0';
    ngx_pfree(r->pool,strinput.data);
    strinput.data = unescape;

    /* download cache */
    if((0 < strdownload.len) && (NULL != strdownload.data))
    {
        if (ngx_strncmp(strdownload.data, DOWNLODA_FROM_ST, ngx_strlen(DOWNLODA_FROM_ST)) == 0) {
            /* TODO: download from http */
            ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap download from standard http file server.");
        }
        else if (ngx_strncmp(strdownload.data, DOWNLODA_FROM_VCN, ngx_strlen(DOWNLODA_FROM_VCN)) == 0) {
            ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap download from HUAWEI VCN file server.");
            ngx_media_snap_from_vcn(r,&strinput,&reqfile,&strFilter);
        }
        else {
            return NGX_ERROR;
        }

        return NGX_DECLINED;
    }


    /* jpeg file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_JPEG,4);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the jpeg.");
        ngx_media_snap_image(r,&strinput,&reqfile,width,heigth,offset,&strFilter);
        return NGX_DECLINED;
    }
    /* jpg file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_JPG,3);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the jpg.");
        ngx_media_snap_image(r,&strinput,&reqfile,width,heigth,offset,&strFilter);
        return NGX_DECLINED;
    }
    /* png file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_PNG,3);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the png.");
        ngx_media_snap_image(r,&strinput,&reqfile,width,heigth,offset,&strFilter);
        return NGX_DECLINED;
    }
    /* bmp file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_BMP,3);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the bmp.");
        ngx_media_snap_image(r,&strinput,&reqfile,width,heigth,offset,&strFilter);
        return NGX_DECLINED;
    }

    /* gif file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_GIF,3);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the gif.");
        ngx_media_snap_gif(r,&strinput,&reqfile,width,heigth,offset,duration,fps,&strScale,&strSpeed,&strFilter);
        return NGX_DECLINED;
    }

    /* mp4 file */
    last = ngx_strcasestrn(r->uri.data,SUFFIXES_MP4,3);
    if(NULL != last)
    {
        ngx_log_debug0(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx http vido snap request to snap the mp4.");
        if(video_copy) {
            width  = -1;
            heigth = -1;
        }
        ngx_media_snap_mp4(r,&strinput,&reqfile,width,heigth,offset,duration,fps,&strScale,&strSpeed,&strFilter);
        return NGX_DECLINED;
    }

    return NGX_DECLINED;
}

static void *
ngx_media_snap_create_loc_conf(ngx_conf_t *cf)
{
    ngx_media_snap_loc_conf_t* conf = NULL;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_snap_loc_conf_t));
    if (conf == NULL)
    {
        return NULL;
    }
    ngx_str_null(&conf->source_dir);
    conf->snap_width     = NGX_CONF_UNSET;
    conf->snap_height    = NGX_CONF_UNSET;
    conf->snap_offset    = NGX_CONF_UNSET;
    conf->snap_duration  = NGX_CONF_UNSET;
    conf->snap_fps       = NGX_CONF_UNSET;

    return conf;
}

static char*
ngx_media_snap_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_snap_handler;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_media_snap_init_worker(ngx_cycle_t *cycle)
{
    /*init the media kenerl libary */
    u_char  licfile[LICENSE_FILE_PATH_LEN];
    ngx_memzero(licfile, LICENSE_FILE_PATH_LEN);

    ngx_str_t* license_file = ngx_media_license_file_path();
    if(NULL == license_file) {
        return NGX_ERROR;
    }

    u_char* last = ngx_snprintf(licfile, LICENSE_FILE_PATH_LEN,"%V/%V",&cycle->conf_prefix,license_file);
    *last = '\0';

    if(MK_ERROR_CODE_OK != mk_lib_init((const char*)&licfile[0],1)) {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"init the mediakernel lib fail.");
        return NGX_ERROR;
    }

    return NGX_OK;
}
static void
ngx_media_snap_exit_worker(ngx_cycle_t *cycle)
{
    /*mk_lib_release();*/
    return;
}


