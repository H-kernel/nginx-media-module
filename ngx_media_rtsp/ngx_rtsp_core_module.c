
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Winshining
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include "ngx_rtsp.h"

extern ngx_uint_t  ngx_rtsp_max_module;

extern ngx_module_t  ngx_rtsp_module;

static ngx_int_t ngx_rtsp_core_preconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_rtsp_core_postconfiguration(ngx_conf_t *cf);
static void     *ngx_rtsp_core_create_main_conf(ngx_conf_t *cf);
static char     *ngx_rtsp_core_init_main_conf(ngx_conf_t *cf, void *conf);
static void     *ngx_rtsp_core_create_srv_conf(ngx_conf_t *cf);
static char     *ngx_rtsp_core_merge_srv_conf(ngx_conf_t *cf, void *parent,void *child);
static void     *ngx_rtsp_core_create_app_conf(ngx_conf_t *cf);
static char     *ngx_rtsp_core_merge_app_conf(ngx_conf_t *cf, void *parent,void *child);
static char     *ngx_rtsp_core_server(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);
static char     *ngx_rtsp_core_application(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);

static ngx_int_t ngx_rtsp_core_init_process(ngx_cycle_t *cycle);


static ngx_command_t  ngx_rtsp_core_commands[] = {

    { ngx_string("server"),
      NGX_RTSP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_rtsp_core_server,
      0,
      0,
      NULL },

    { ngx_string("listen"),
      NGX_RTSP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_RTSP_SRV_CONF_OFFSET,
      offsetof(ngx_rtsp_core_srv_conf_t, port),
      NULL },

    { ngx_string("application"),
      NGX_RTSP_SRV_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
      ngx_rtsp_core_application,
      NGX_RTSP_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_rtsp_module_t  ngx_rtsp_core_module_ctx = {
    ngx_rtsp_core_preconfiguration,         /* preconfiguration */
    ngx_rtsp_core_postconfiguration,        /* postconfiguration */
    ngx_rtsp_core_create_main_conf,         /* create main configuration */
    ngx_rtsp_core_init_main_conf,           /* init main configuration */
    ngx_rtsp_core_create_srv_conf,          /* create server configuration */
    ngx_rtsp_core_merge_srv_conf,           /* merge server configuration */
    ngx_rtsp_core_create_app_conf,          /* create app configuration */
    ngx_rtsp_core_merge_app_conf            /* merge app configuration */
};


ngx_module_t  ngx_rtsp_core_module = {
    NGX_MODULE_V1,
    &ngx_rtsp_core_module_ctx,             /* module context */
    ngx_rtsp_core_commands,                /* module directives */
    NGX_RTSP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_rtsp_core_init_process,            /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_rtsp_core_preconfiguration(ngx_conf_t *cf)
{
    /* nothing to do */
	return NGX_OK;
}

static ngx_int_t
ngx_rtsp_core_postconfiguration(ngx_conf_t *cf)
{
	return NGX_OK;
}


static void *
ngx_rtsp_core_create_main_conf(ngx_conf_t *cf)
{
    ngx_rtsp_core_main_conf_t  *cmcf;

    cmcf = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_core_main_conf_t));
    if (cmcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&cmcf->servers, cf->pool, 4,
                       sizeof(ngx_rtsp_core_srv_conf_t *))
        != NGX_OK)
    {
        return NULL;
    }

    return cmcf;
}


static char *
ngx_rtsp_core_init_main_conf(ngx_conf_t *cf, void *conf)
{
    return NGX_CONF_OK;
}


static void *
ngx_rtsp_core_create_srv_conf(ngx_conf_t *cf)
{
    ngx_rtsp_core_srv_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_core_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->applications, cf->pool, 4,
                       sizeof(ngx_rtsp_core_app_conf_t *))
        != NGX_OK)
    {
        return NULL;
    }
    conf->handle = NULL;
    conf->port   = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_rtsp_core_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtsp_core_srv_conf_t *prev = parent;
    ngx_rtsp_core_srv_conf_t *conf = child;

    if (NULL == prev->handle) { 
        prev->handle = conf->handle;
    }

    ngx_conf_merge_uint_value(prev->port, conf->port, 554);

    return NGX_CONF_OK;
}


static void *
ngx_rtsp_core_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtsp_core_app_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_core_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->applications, cf->pool, 1,
                       sizeof(ngx_rtsp_core_app_conf_t *))
        != NGX_OK)
    {
        return NULL;
    }
    return conf;
}


static char *
ngx_rtsp_core_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtsp_core_app_conf_t *prev = parent;
    ngx_rtsp_core_app_conf_t *conf = child;

    ngx_rtsp_core_srv_conf_t* csconf 
            = ngx_rtsp_conf_get_module_srv_conf(cf,ngx_rtsp_core_module);
    if(NULL == conf->handle)  {
        conf->handle = csconf->handle;
    }      
    if (NULL == prev->handle) { 
        prev->handle = conf->handle;
    }
    return NGX_CONF_OK;
}


static char *
ngx_rtsp_core_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                       *rv;
    void                       *mconf;
    ngx_uint_t                  m;
    ngx_conf_t                  pcf;
    ngx_module_t              **modules;
    ngx_rtsp_module_t          *module;
    ngx_rtsp_conf_ctx_t        *ctx, *rtsp_ctx;
    ngx_rtsp_core_srv_conf_t   *cscf, **cscfp;
    ngx_rtsp_core_main_conf_t  *cmcf;

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    rtsp_ctx = cf->ctx;
    ctx->main_conf = rtsp_ctx->main_conf;

    /* the server{}'s srv_conf */

    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    ctx->app_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->app_conf == NULL) {
        return NGX_CONF_ERROR;
    }

#if (nginx_version >= 1009011)
    modules = cf->cycle->modules;
#else
    modules = ngx_modules;
#endif

    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTSP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;

        if (module->create_srv_conf) {
            mconf = module->create_srv_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }

            ctx->srv_conf[modules[m]->ctx_index] = mconf;
        }

        if (module->create_app_conf) {
            mconf = module->create_app_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }

            ctx->app_conf[modules[m]->ctx_index] = mconf;
        }
    }

    /* the server configuration context */

    cscf = ctx->srv_conf[ngx_rtsp_core_module.ctx_index];
    cscf->ctx = ctx;
    cscf->handle  = ngx_pcalloc(cf->pool, sizeof(AS_HANDLE));
    *cscf->handle = NULL;

    cmcf = ctx->main_conf[ngx_rtsp_core_module.ctx_index];

    cscfp = ngx_array_push(&cmcf->servers);
    if (cscfp == NULL) {
        return NGX_CONF_ERROR;
    }

    *cscfp = cscf;


    /* parse inside server{} */

    pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = NGX_RTSP_SRV_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    return rv;
}

static char *
ngx_rtsp_core_application(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                       *rv;
    ngx_int_t                   i;
    ngx_uint_t                  n;
    ngx_str_t                  *value;
    ngx_conf_t                  save;
    ngx_module_t              **modules;
    ngx_rtsp_module_t          *module;
    ngx_rtsp_conf_ctx_t        *ctx, *pctx;
    ngx_rtsp_core_srv_conf_t   *cscf;
    ngx_rtsp_core_app_conf_t   *cacf, **cacfp;

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    pctx = cf->ctx;
    ctx->main_conf = pctx->main_conf;
    ctx->srv_conf = pctx->srv_conf;

    ctx->app_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->app_conf == NULL) {
        return NGX_CONF_ERROR;
    }

#if (nginx_version >= 1009011)
    modules = cf->cycle->modules;
#else
    modules = ngx_modules;
#endif

    for (i = 0; modules[i]; i++) {
        if (modules[i]->type != NGX_RTSP_MODULE) {
            continue;
        }

        module = modules[i]->ctx;

        if (module->create_app_conf) {
            ctx->app_conf[modules[i]->ctx_index] = module->create_app_conf(cf);
            if (ctx->app_conf[modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    cacf = ctx->app_conf[ngx_rtsp_core_module.ctx_index];
    cacf->app_conf = ctx->app_conf;

    value = cf->args->elts;

    cacf->name = value[1];
    cscf = pctx->srv_conf[ngx_rtsp_core_module.ctx_index];
    cacf->handle = cscf->handle;

    cacfp = cscf->applications.elts;
    for (n = 0; n < cscf->applications.nelts; n++) {
        if (cacf->name.len == cacfp[n]->name.len
                && ngx_strncmp(cacf->name.data,
                        cacfp[n]->name.data, cacf->name.len) == 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "duplicate application: \"%V\"", &cacf->name);

            return NGX_CONF_ERROR;
        }
    }

    cacfp = ngx_array_push(&cscf->applications);
    if (cacfp == NULL) {
        return NGX_CONF_ERROR;
    }

    *cacfp = cacf;

    save = *cf;
    cf->ctx = ctx;
    cf->cmd_type = NGX_RTSP_APP_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf= save;

    return rv;
}

static ngx_int_t 
ngx_rtsp_core_init_process(ngx_cycle_t *cycle)
{
    ngx_uint_t                   s;
    ngx_rtsp_core_srv_conf_t    *cscf, **cscfp;
    
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_media_rtsp_module,the process:[%d] is not 0,no need init rtsp core.",ngx_process_slot);
        return NGX_OK;
    }

    ngx_rtsp_core_main_conf_t* conf 
            = ngx_rtsp_cycle_get_module_main_conf(cycle,ngx_rtsp_core_module);
    if(NULL == conf) {
        return NGX_OK;
    }
    cscfp = conf->servers.elts;
    for (s = 0; s < conf->servers.nelts; s++) {

        cscf = cscfp[s]->ctx->srv_conf[ngx_rtsp_core_module.ctx_index];
        if(NGX_CONF_UNSET_UINT == cscf->port) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,server no listen port.");
            continue;
        }
        uint16_t port = (uint16_t)cscf->port;
        *cscf->handle = as_rtsp_svr_create_handle(port);
        if(NULL == *cscf->handle) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,create server handle fail.listen port:[%d].",port);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
