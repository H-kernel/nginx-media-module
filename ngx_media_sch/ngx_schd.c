
/*
 * Copyright (C) H.Kernel
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include "ngx_schd.h"

extern ngx_int_t    ngx_ncpu;

static char *ngx_schd_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_schd_init_process(ngx_cycle_t *cycle);
static void      ngx_schd_exit_process(ngx_cycle_t *cycle);



ngx_uint_t  ngx_schd_max_module;


static ngx_command_t  ngx_schd_commands[] = {

    { ngx_string("schd"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_schd_block,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_schd_module_ctx = {
    ngx_string("schd"),
    NULL,
    NULL
};


ngx_module_t  ngx_schd_module = {
    NGX_MODULE_V1,
    &ngx_schd_module_ctx,                  /* module context */
    ngx_schd_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_schd_init_process,                 /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_schd_exit_process,                 /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_schd_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_schd_module_t    *m;

    /* count the number of the event modules and set up their indices */

    ngx_schd_max_module = ngx_count_modules(cf->cycle, NGX_SCHD_MODULE);

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_schd_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(void **) conf = ctx;

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SCHD_MODULE) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->create_conf) {
            (*ctx)[cf->cycle->modules[i]->ctx_index] =
                                                     m->create_conf(cf->cycle);
            if ((*ctx)[cf->cycle->modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_SCHD_MODULE;
    cf->cmd_type = NGX_SCHD_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SCHD_MODULE) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->init_conf) {
            rv = m->init_conf(cf->cycle,
                              (*ctx)[cf->cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_schd_init_process(ngx_cycle_t *cycle)
{
    return NGX_OK;
}
static void      
ngx_schd_exit_process(ngx_cycle_t *cycle)
{
    return ;
}

