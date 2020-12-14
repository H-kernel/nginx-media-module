/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_probe_module.c
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

#define REQ_ARG_VPATH        "vpath"
#define REQ_ARG_VFILE        "vfile"

#define REQ_ARG_V_LEN    5

#define SNAP_MK_PARAM_MAX     32



static char* ngx_media_probe_name = "ngx_media_probe";

typedef struct {
    ngx_str_t                      source_dir;
} ngx_media_probe_loc_conf_t;


static char*     ngx_media_probe_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*     ngx_media_probe_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_media_probe_init_worker(ngx_cycle_t *cycle);
static void      ngx_media_probe_exit_worker(ngx_cycle_t *cycle);



static ngx_command_t  ngx_media_probe_commands[] = {

    { ngx_string("video_probe"),
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      ngx_media_probe_init,
      0,
      0,
      NULL },

    { ngx_string("probe_source_dir"),
      NGX_HTTP_LOC_CONF |NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_probe_loc_conf_t, source_dir),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_probe_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_media_probe_create_loc_conf,        /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_probe_module = {
    NGX_MODULE_V1,
    &ngx_media_probe_module_ctx,            /* module context */
    ngx_media_probe_commands,               /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_media_probe_init_worker,            /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    ngx_media_probe_exit_worker,            /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void
ngx_media_probe_from_media(ngx_http_request_t *r,ngx_str_t *input,ngx_str_t* output)
{

    char       *paramlist[SNAP_MK_PARAM_MAX];
    ngx_int_t   paramcount = 0;
    MK_HANDLE   handle     = NULL;
    u_char     *last       = NULL,*start = NULL,*end = NULL,*name = NULL,*value = NULL;
    u_char     *finish     = NULL,*equal = NULL;
    ngx_uint_t  len        = 0;
    ngx_int_t   i          = 0;

    /* create the mk handle to capture the image */
    handle = mk_create_handle(ngx_media_probe_name,MK_HANDLE_TYPE_PROBE);
    if(NULL == handle) {
        return;
    }


    start = r->args.data;
    finish = start + r->args.len;

    for ( /* void */ ; start < finish; /* void */) {

        /* find the args split & */
        end = ngx_strlchr(start, finish, '&');
        if (end == NULL) {
            if(start < finish) {
                end = finish;/* last one */
            }
            else {
                break;
            }
        }

        /* we need '=' after name */
        equal = ngx_strlchr(start, end, '=');

        /* arg name */
        if (NULL == equal) {
            len = (end - start);
        }
        else {
            len = (equal - start);
        }

        if((REQ_ARG_V_LEN == len)
          &&((0 == ngx_memcmp(start,REQ_ARG_VFILE,REQ_ARG_V_LEN)) 
            || (0 == ngx_memcmp(start,REQ_ARG_VPATH,REQ_ARG_V_LEN))))
        {
            start = end + 1;/* skip the & */
            continue;
        }
        name = ngx_pcalloc(r->pool,(len + 2));

        name[0] = '-';

        last = ngx_cpymem(&name[1],start,len);
        *last = '\0';
        /* arg name */
        paramlist[paramcount] = (char*)&name[0];                paramcount++;
        
        if (NULL == equal) {
            start = end + 1;/* skip the & */
            ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,"drop the src arg");
            continue;
        }

        start = equal + 1;/* skip the = */

        len = (end - start);
        value = ngx_pcalloc(r->pool,(len + 1));

        last = ngx_cpymem(value,start,len);
        *last = '\0';
        paramlist[paramcount] = (char*)&value[0];                paramcount++;

        start = end + 1;/* skip the & */
    }

    paramlist[paramcount] = "-src";                paramcount++;
    paramlist[paramcount] = (char*)input->data;    paramcount++;

    paramlist[paramcount] = "-dst";                paramcount++;
    paramlist[paramcount] = (char*)output->data;   paramcount++; 

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "\t the probe media:[%s],begin:",input->data);
    
    for(i = 0;i < paramcount;i++) {
        name = (u_char*)paramlist[i];
        value = NULL;
        if((i + 1) < paramcount) {
            value = (u_char*)paramlist[i+1];
        }

        if(NULL != value) {
            if('-' != value[0]) {
                ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                        "\t\t arg:[%s] value:[%s]",name,value);
                i++;
                continue;
            }
        }
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                        "\t\t arg:[%s]",name);
    }
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "\t the probe media:[%s],end:",input->data);

    int32_t ret = mk_do_task(handle, paramcount,(char**)paramlist);

    if(MK_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log,0,
                          "ngx media probe,probe intput:[%V] to file:[%V] fail.",input,output);
    }

    /* destory the mk handle */
    if(NULL != handle) {
        mk_destory_handle(handle);
        handle = NULL;
    }

    return;
}


static ngx_int_t
ngx_media_probe_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    ngx_media_probe_loc_conf_t    *conf;
    ngx_file_info_t                fi;
    u_char                        *last;
    size_t                         root;
    ngx_str_t                      reqfile;

    ngx_str_t                      vpath;
    ngx_str_t                      vfile;

    ngx_str_t                      strinput;

    ngx_str_null(&reqfile);
    ngx_str_null(&vpath);
    ngx_str_null(&vfile);
    ngx_str_null(&strinput);
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                          "ngx media handle probe request.");


    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media probe request method is invalid.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media vido probe request uri is invalid.");
        return NGX_DECLINED;
    }

    /* discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_media_probe_module);


    /* 1. check the file exist */
    last = ngx_http_map_uri_to_path(r, &reqfile, &root, 0);
    if (NULL == last)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the media probe reuquest file path is not exist.");
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
    if (0 == r->args.len) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the media probe request have no args.");
        return NGX_HTTP_NOT_FOUND;
    }
    if (ngx_http_arg(r, (u_char *) REQ_ARG_VFILE, REQ_ARG_V_LEN, &vfile) != NGX_OK) {
        vfile.data = NULL;
        vfile.len  = 0;
    }
    else if (ngx_http_arg(r, (u_char *) REQ_ARG_VPATH, REQ_ARG_V_LEN, &vpath) != NGX_OK) {
        vpath.data = NULL;
        vpath.len  = 0;
    }


    if((0 < vfile.len) &&(NULL != vfile.data)) {
        if((0 < vpath.len) &&(NULL != vpath.data)) {
            if(NULL == ngx_media_sys_map_vpath_vfile_to_path(r,&vpath,&vfile,&strinput)) {
                return NGX_ERROR;
            }
        }
        else if((0 < conf->source_dir.len) &&(NULL != conf->source_dir.data)){
            strinput.len = conf->source_dir.len + vfile.len + 2;
            strinput.data = ngx_pcalloc(r->pool,strinput.len);
            if('/' == conf->source_dir.data[conf->source_dir.len - 1]) {
                last = ngx_snprintf(strinput.data,strinput.len,"%V%V",&conf->source_dir,&vfile);
            }
            else {
                last = ngx_snprintf(strinput.data,strinput.len,"%V/%V",&conf->source_dir,&vfile);
            }
            *last = '\0';
        }
        else {
            return NGX_ERROR;
        }
    }
    else if((0 < vpath.len) &&(NULL != vpath.data)) {
        u_char* unescape = ngx_pcalloc(r->pool,vpath.len+1);
        u_char* pszDst = unescape;
        ngx_unescape_uri(&pszDst,&vpath.data, vpath.len, 0);
        pszDst = '\0';

        strinput.data = unescape;
        strinput.len  = vpath.len + 1;
    }
    else {
        return NGX_ERROR;
    }

    if(0 == strinput.len)
    {
        return NGX_ERROR;
    }
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "the media probe reuquest file'%V' input'%V'.",&reqfile,&strinput);

    ngx_media_probe_from_media(r,&strinput,&reqfile);       

    return NGX_DECLINED;
}

static void *
ngx_media_probe_create_loc_conf(ngx_conf_t *cf)
{
    ngx_media_probe_loc_conf_t* conf = NULL;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_probe_loc_conf_t));
    if (conf == NULL)
    {
        return NULL;
    }
    ngx_str_null(&conf->source_dir);

    return conf;
}

static char*
ngx_media_probe_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_probe_handler;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_media_probe_init_worker(ngx_cycle_t *cycle)
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
ngx_media_probe_exit_worker(ngx_cycle_t *cycle)
{
    /*mk_lib_release();*/
    return;
}


