
/*
 * Copyright (C) H.Kernel
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include "ngx_rtsp.h"
#include <libasrtspsvr/libRtspSvr.h>

extern ngx_int_t    ngx_ncpu;

static char *ngx_rtsp_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char * ngx_rtsp_merge_applications(ngx_conf_t *cf,
        ngx_array_t *applications, void **app_conf, ngx_rtsp_module_t *module,
        ngx_uint_t ctx_index);
static ngx_int_t ngx_rtsp_init_process(ngx_cycle_t *cycle);
static void      ngx_rtsp_exit_process(ngx_cycle_t *cycle);



ngx_uint_t  ngx_rtsp_max_module;


static ngx_command_t  ngx_rtsp_commands[] = {

    { ngx_string("rtsp"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_rtsp_block,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_rtsp_module_ctx = {
    ngx_string("rtsp"),
    NULL,
    NULL
};


ngx_module_t  ngx_rtsp_module = {
    NGX_MODULE_V1,
    &ngx_rtsp_module_ctx,                  /* module context */
    ngx_rtsp_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_rtsp_init_process,                 /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_rtsp_exit_process,                 /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_rtsp_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                        *rv;
    ngx_uint_t                   m, mi, s;
    ngx_conf_t                   pcf;
    ngx_module_t               **modules;
    ngx_rtsp_module_t           *module;
    ngx_rtsp_conf_ctx_t         *ctx;
    ngx_rtsp_core_srv_conf_t    *cscf, **cscfp;
    ngx_rtsp_core_main_conf_t   *cmcf;

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_rtsp_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(ngx_rtsp_conf_ctx_t **) conf = ctx;

    /* count the number of the rtsp modules and set up their indices */
#if (nginx_version >= 1009011)
    ngx_rtsp_max_module = ngx_count_modules(cf->cycle, NGX_RTSP_MODULE);
#else
    ngx_rtsp_max_module = 0;
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_RTSP_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = ngx_rtsp_max_module++;
    }
#endif


    /* the rtsp main_conf context, it is the same in the all rtsp contexts */
    ctx->main_conf = ngx_pcalloc(cf->pool,
                                 sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->main_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * the rtsp null srv_conf context, it is used to merge
     * the server{}s' srv_conf's
     */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * the rtsp null app_conf context, it is used to merge
     * the server{}s' app_conf's
     */
    ctx->app_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtsp_max_module);
    if (ctx->app_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * create the main_conf's, the null srv_conf's, and the null app_conf's
     * of the all rtsp modules
     */

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
        mi = modules[m]->ctx_index;

        if (module->create_main_conf) {
            ctx->main_conf[mi] = module->create_main_conf(cf);
            if (ctx->main_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_srv_conf) {
            ctx->srv_conf[mi] = module->create_srv_conf(cf);
            if (ctx->srv_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_app_conf) {
            ctx->app_conf[mi] = module->create_app_conf(cf);
            if (ctx->app_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;

    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTSP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;

        if (module->preconfiguration) {
            if (module->preconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* parse inside the rtsp{} block */

    cf->module_type = NGX_RTSP_MODULE;
    cf->cmd_type = NGX_RTSP_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        *cf = pcf;
        return rv;
    }


    /* init rtsp{} main_conf's, merge the server{}s' srv_conf's */

    cmcf = ctx->main_conf[ngx_rtsp_core_module.ctx_index];
    cscfp = cmcf->servers.elts;

    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTSP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;
        mi = modules[m]->ctx_index;

        /* init rtsp{} main_conf's */

        cf->ctx = ctx;

        if (module->init_main_conf) {
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);
            if (rv != NGX_CONF_OK) {
                *cf = pcf;
                return rv;
            }
        }

        for (s = 0; s < cmcf->servers.nelts; s++) {

            /* merge the server{}s' srv_conf's */

            cf->ctx = cscfp[s]->ctx;

            if (module->merge_srv_conf) {
                rv = module->merge_srv_conf(cf,
                                            ctx->srv_conf[mi],
                                            cscfp[s]->ctx->srv_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }
            }

            if (module->merge_app_conf) {

                /* merge the server{}'s app_conf */

                /*ctx->app_conf = cscfp[s]->ctx->app_conf;*/

                rv = module->merge_app_conf(cf,
                                            ctx->app_conf[mi],
                                            cscfp[s]->ctx->app_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }

                /* merge the applications{}' app_conf's */

                cscf = cscfp[s]->ctx->srv_conf[ngx_rtsp_core_module.ctx_index];

                rv = ngx_rtsp_merge_applications(cf, &cscf->applications,
                                            cscfp[s]->ctx->app_conf,
                                            module, mi);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }
            }

        }
    }

    cf->ctx = ctx;

    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTSP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;

        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    *cf = pcf;

    return NGX_CONF_OK;
}


static char *
ngx_rtsp_merge_applications(ngx_conf_t *cf, ngx_array_t *applications,
            void **app_conf, ngx_rtsp_module_t *module, ngx_uint_t ctx_index)
{
    char                           *rv;
    ngx_rtsp_conf_ctx_t            *ctx, saved;
    ngx_rtsp_core_app_conf_t      **cacfp;
    ngx_uint_t                      n;
    ngx_rtsp_core_app_conf_t       *cacf;

    if (applications == NULL) {
        return NGX_CONF_OK;
    }

    ctx = (ngx_rtsp_conf_ctx_t *) cf->ctx;
    saved = *ctx;

    cacfp = applications->elts;
    for (n = 0; n < applications->nelts; ++n, ++cacfp) {

        ctx->app_conf = (*cacfp)->app_conf;

        rv = module->merge_app_conf(cf, app_conf[ctx_index],
                (*cacfp)->app_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }

        cacf = (*cacfp)->app_conf[ngx_rtsp_core_module.ctx_index];
        rv = ngx_rtsp_merge_applications(cf, &cacf->applications,
                                         (*cacfp)->app_conf,
                                         module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    *ctx = saved;

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_rtsp_init_process(ngx_cycle_t *cycle)
{
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_media_rtsp_module,the process:[%d] is not 0,no need init rtsp library.",ngx_process_slot);
        return NGX_OK;
    }
    if(0 == ngx_rtsp_max_module) {
        ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "ngx_media_rtsp_module,the process:[%d] there is no rtsp block:[%d].",ngx_process_slot,ngx_rtsp_max_module);
        return NGX_OK;
    }
    /* init the rtsp library */
    uint32_t ulCPUCount = (uint32_t)ngx_ncpu;
    if(0 != as_rtsp_svr_init(ulCPUCount)) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_rtsp_module,init rtsp lib fail.cpu:[%d].",ulCPUCount);
        return NGX_ERROR;
    }
    return NGX_OK;
}
static void      
ngx_rtsp_exit_process(ngx_cycle_t *cycle)
{
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "ngx_media_rtsp_module,the process:[%d] is not 0,no need release rtsp library.",ngx_process_slot);
        return ;
    }
    /* release the rtsp library */
    as_rtsp_svr_release();
    return ;
}

