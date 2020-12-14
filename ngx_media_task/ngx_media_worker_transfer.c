#include "ngx_media_worker_transfer.h"
#include<curl/curl.h>


#define VIDEO_TRANSFER_ARG_TRANSTYPE      "-trans_type"
#define VIDEO_TRANSFER_ARG_DELETE         "-delete"
#define VIDEO_TRANSFER_ARG_SOURCE         "-src"
#define VIDEO_TRANSFER_ARG_DESTINATION    "-dst"
#define VIDEO_TRANSFER_ARG_SRC_DIR        "-src_dir"
#define VIDEO_TRANSFER_ARG_DST_DIR        "-dst_dir"
#define VIDEO_TRANSFER_ARG_SRC_FS         "-src_fs"
#define VIDEO_TRANSFER_ARG_DST_FS         "-dst_fs"


enum NGX_WORKER_PATH_TYPE {
    NGX_WORKER_PATH_TYPE_DIR        = 0,   /* DIRECTORY */
    NGX_WORKER_PATH_TYPE_FILE       = 1,   /* FILE */
};

#define NGX_WORKER_TRANS_TYPE_STR_UPLOAD   "upload"
#define NGX_WORKER_TRANS_TYPE_STR_DOWNLOAD "download"
#define NGX_WORKER_TRANS_TYPE_STR_DELETE   "delete"


enum NGX_WORKER_TRANS_TYPE {
    NGX_WORKER_TRANS_TYPE_UPLOAD    = 0,   /* UPLOAD */
    NGX_WORKER_TRANS_TYPE_DOWNLOAD  = 1,   /* DOWNLOAD */
    NGX_WORKER_TRANS_TYPE_DELETE    = 2,   /* DELETE */
};


#define NGX_WORKER_STORAGE_TYPE_STR_FS   "fs"
#define NGX_WORKER_STORAGE_TYPE_STR_URL  "url"

enum NGX_WORKER_STORAGE_TYPE {
    NGX_WORKER_STORAGE_TYPE_LOCAL_FS     = 0,   /* local file system */
    NGX_WORKER_STORAGE_TYPE_REMOTE_URL   = 1,   /* remote system,and access by the standard protocol */
};

typedef struct {
    ngx_uint_t                      type;
    ngx_uint_t                      system;
    ngx_str_t                       path;
}ngx_worker_path;



typedef struct {
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    WK_WATCH                        watcher;
    ngx_media_worker_ctx_t         *wk_ctx;
    pthread_t                       threadid;
    ngx_uint_t                      running;
    ngx_uint_t                      transType;
    ngx_list_t                     *src;
    ngx_worker_path                 dst;
    ngx_uint_t                      src_fs;
    ngx_uint_t                      dst_fs;
    ngx_list_t                     *delete;
} ngx_worker_transfer_ctx_t;


typedef struct {
    FILE                           *fd;
    ngx_int_t                       ret;
} ngx_worker_transfer_write_ctx_t;



#define NGX_WORKER_TRANS_MAX        32

static ngx_int_t  ngx_media_worker_transfer_parser_agrs(ngx_media_worker_ctx_t* ctx);
static ngx_int_t  ngx_media_worker_transfer_check_agrs(ngx_media_worker_ctx_t* ctx);
static ngx_int_t  ngx_media_worker_transfer_find_arg(ngx_worker_transfer_ctx_t* ctx,u_char* arg,u_char* value,ngx_int_t* index);
static ngx_int_t  ngx_media_worker_transfer_arg_trans_type(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_delete(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_src(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_dst(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_src_dir(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_dst_dir(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_src_fs(ngx_worker_transfer_ctx_t* ctx,u_char* value);
static ngx_int_t  ngx_media_worker_transfer_arg_dst_fs(ngx_worker_transfer_ctx_t* ctx,u_char* value);

static void *     ngx_media_worker_transfer_thread(void *data);
static void       ngx_media_worker_transfer_upload(ngx_worker_transfer_ctx_t* ctx);
static void       ngx_media_worker_transfer_download(ngx_worker_transfer_ctx_t* ctx);

static void       ngx_media_worker_transfer_upload_file(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file_path);
static void       ngx_media_worker_transfer_upload_dir(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * dir_path);
static ngx_int_t  ngx_media_worker_transfer_upload_tree_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t  ngx_media_worker_transfer_upload_tree_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t  ngx_media_worker_transfer_upload_tree_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static void       ngx_media_worker_transfer_upload_file_standard(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file);


static void       ngx_media_worker_transfer_download_file(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file);
static size_t     ngx_media_worker_transfer_download_file_standard_fwrite(void *buffer, size_t size, size_t nmemb, void *stream);
static void       ngx_media_worker_transfer_download_file_standard(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file);

static void       ngx_media_worker_transfer_local_copy(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file);

static ngx_int_t  ngx_media_worker_transfer_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t  ngx_media_worker_transfer_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t  ngx_media_worker_transfer_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static void       ngx_media_worker_transfer_delete(ngx_worker_transfer_ctx_t* ctx);




static ngx_int_t
ngx_media_worker_transfer_parser_agrs(ngx_media_worker_ctx_t* ctx)
{
    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)ctx->priv_data;

    ngx_int_t ret = NGX_OK;
    ngx_int_t i = 0;

    for(i = 0; i < ctx->nparamcount;i++) {
        u_char* arg   = ctx->paramlist[i];
        u_char* value = NULL;
        if(i < (ctx->nparamcount -1)) {
            value = ctx->paramlist[i+1];
        }

        if('-' == arg[0]) {
            ret = ngx_media_worker_transfer_find_arg(trans_ctx,arg,value,&i);
            if(NGX_OK != ret) {
                return NGX_ERROR;
            }
        }
        else
        {
            /* unknow the arg list,skip it */
            continue;
        }
    }
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_check_agrs(ngx_media_worker_ctx_t* ctx)
{
    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)ctx->priv_data;

    if(NGX_WORKER_TRANS_TYPE_DELETE == trans_ctx->transType){
        ngx_list_part_t *part  = &(trans_ctx->delete->part);
        if(0 == part->nelts) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 2,
                          "ngx_media_worker_transfer_check_agrs,the delete list is empty.");
            return NGX_ERROR;
        }
        return NGX_OK;
    }

    if(0 == trans_ctx->dst.path.len) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 2,
                          "ngx_media_worker_transfer_check_agrs,the dst is empty.");
        return NGX_ERROR;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_find_arg(ngx_worker_transfer_ctx_t* ctx,u_char* arg,u_char* value,ngx_int_t* index)
{
    ngx_int_t ret = NGX_OK;

    size_t size = ngx_strlen(arg);

    if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_TRANSTYPE,size) == 0) {
        ret = ngx_media_worker_transfer_arg_trans_type(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_DELETE,size) == 0) {
        ret = ngx_media_worker_transfer_arg_delete(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_SOURCE,size) == 0) {
        ret = ngx_media_worker_transfer_arg_src(ctx,value);
        if(NGX_OK == ret) {
           (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_DESTINATION,size) == 0) {
        ret = ngx_media_worker_transfer_arg_dst(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_SRC_DIR,size) == 0) {
        ret = ngx_media_worker_transfer_arg_src_dir(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_DST_DIR,size) == 0) {
        ret = ngx_media_worker_transfer_arg_dst_dir(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_SRC_FS,size) == 0) {
        ret = ngx_media_worker_transfer_arg_src_fs(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,VIDEO_TRANSFER_ARG_DST_FS,size) == 0) {
        ret = ngx_media_worker_transfer_arg_dst_fs(ctx,value);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 2,
                          "ngx_media_worker_transfer_find_arg unknow the args :[%s] value:[%s].",arg,value);
        return NGX_ERROR;
    }

    return ret;
}

static ngx_int_t
ngx_media_worker_transfer_arg_trans_type(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    size_t size = ngx_strlen(value);
    if(ngx_strncmp(value,NGX_WORKER_TRANS_TYPE_STR_UPLOAD,size) == 0) {
        ctx->transType = NGX_WORKER_TRANS_TYPE_UPLOAD;
    }
    else if(ngx_strncmp(value,NGX_WORKER_TRANS_TYPE_STR_DOWNLOAD,size) == 0) {
        ctx->transType = NGX_WORKER_TRANS_TYPE_DOWNLOAD;
    }
    else if(ngx_strncmp(value,NGX_WORKER_TRANS_TYPE_STR_DELETE,size) == 0) {
        ctx->transType = NGX_WORKER_TRANS_TYPE_DELETE;
    }
    else {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 1,
                          "ngx_media_worker_transfer_arg_trans_type unknow the trans type :[%s] .",value);
        return NGX_ERROR;
    }
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_arg_delete(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    ngx_str_t* str = ngx_list_push(ctx->delete);
    if(NULL == str) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 1,
                          "ngx_media_worker_transfer_arg_action ,get delete list node fail.");
        return NGX_ERROR;
    }
    size_t size = ngx_strlen(value);

    str->data = ngx_pcalloc(ctx->pool,size+1);
    str->len  = size;

    u_char* last = ngx_copy(str->data,value,size);
    *last ='\0';
    ngx_log_error(NGX_LOG_ERR, ctx->log, 1,
                          "ngx_media_worker_transfer_arg_action,add the delete path:[%s] .",value);
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_arg_src(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    ngx_worker_path* src = ngx_list_push(ctx->src);

    if(NULL == src) {
        return NGX_ERROR;
    }

    src->type    = NGX_WORKER_PATH_TYPE_FILE;
    src->system  = ctx->src_fs;

    ngx_int_t ret = ngx_media_worker_arg_value_str(ctx->pool,value,&src->path);
    if(NGX_OK != ret) {
        return NGX_ERROR;
    }

    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_arg_dst(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    if(0 < ctx->dst.path.len) {
        /* the dst only set once */
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_arg_dst,the dst arg have inited.");
        return NGX_ERROR;
    }

    ctx->dst.type    = NGX_WORKER_PATH_TYPE_FILE;
    ctx->dst.system  = ctx->dst_fs;

    ngx_int_t ret = ngx_media_worker_arg_value_str(ctx->pool,value,&ctx->dst.path);
    if(NGX_OK != ret) {
        return NGX_ERROR;
    }
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_arg_src_dir(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    ngx_worker_path* src = ngx_list_push(ctx->src);

    if(NULL == src) {
        return NGX_ERROR;
    }

    src->type    = NGX_WORKER_PATH_TYPE_DIR;
    src->system  = ctx->src_fs;

    ngx_int_t ret = ngx_media_worker_arg_value_str(ctx->pool,value,&src->path);
    if(NGX_OK != ret) {
        return NGX_ERROR;
    }

    return NGX_OK;

}
static ngx_int_t
ngx_media_worker_transfer_arg_dst_dir(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    if(0 < ctx->dst.path.len) {
        /* the dst only set once */
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_arg_dst_dir,the dst arg have inited.");
        return NGX_ERROR;
    }

    ctx->dst.type    = NGX_WORKER_PATH_TYPE_DIR;
    ctx->dst.system  = ctx->dst_fs;

    ngx_int_t ret = ngx_media_worker_arg_value_str(ctx->pool,value,&ctx->dst.path);
    if(NGX_OK != ret) {
        return NGX_ERROR;
    }
    return NGX_OK;

}
static ngx_int_t
ngx_media_worker_transfer_arg_src_fs(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    size_t size = ngx_strlen(value);
    if(ngx_strncmp(value,NGX_WORKER_STORAGE_TYPE_STR_FS,size) == 0) {
        ctx->src_fs = NGX_WORKER_STORAGE_TYPE_LOCAL_FS;
    }
    else if(ngx_strncmp(value,NGX_WORKER_STORAGE_TYPE_STR_URL,size) == 0) {
        ctx->src_fs = NGX_WORKER_STORAGE_TYPE_REMOTE_URL;
    }
    else {
        return NGX_ERROR;
    }
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_arg_dst_fs(ngx_worker_transfer_ctx_t* ctx,u_char* value)
{
    size_t size = ngx_strlen(value);
    if(ngx_strncmp(value,NGX_WORKER_STORAGE_TYPE_STR_FS,size) == 0) {
        ctx->dst_fs = NGX_WORKER_STORAGE_TYPE_LOCAL_FS;
    }
    else if(ngx_strncmp(value,NGX_WORKER_STORAGE_TYPE_STR_URL,size) == 0) {
        ctx->dst_fs = NGX_WORKER_STORAGE_TYPE_REMOTE_URL;
    }
    else {
        return NGX_ERROR;
    }
    return NGX_OK;
}

static void *
ngx_media_worker_transfer_thread(void *data)
{
    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)data;
    ngx_log_error(NGX_LOG_DEBUG, trans_ctx->log, 0,
                          "ngx_media_worker_transfer_thread begin.");

    if(trans_ctx->watcher) {
        trans_ctx->watcher(ngx_media_worker_status_start,NGX_MEDIA_ERROR_CODE_OK,trans_ctx->wk_ctx);
    }



    if(NGX_WORKER_TRANS_TYPE_UPLOAD == trans_ctx->transType) {
        ngx_media_worker_transfer_upload(trans_ctx);
    }
    else if(NGX_WORKER_TRANS_TYPE_DOWNLOAD == trans_ctx->transType) {
        ngx_media_worker_transfer_download(trans_ctx);
    }
    /* if(NGX_WORKER_TRANS_TYPE_DELETE == trans_ctx->transType)
     or clean the trans temp file */

    ngx_media_worker_transfer_delete(trans_ctx);
    if(trans_ctx->watcher) {
        trans_ctx->watcher(ngx_media_worker_status_end,NGX_MEDIA_ERROR_CODE_OK,trans_ctx->wk_ctx);
    }
    ngx_log_error(NGX_LOG_DEBUG, trans_ctx->log, 0,
                          "ngx_media_worker_transfer_thread end.");
    return NULL;
}

static void
ngx_media_worker_transfer_upload(ngx_worker_transfer_ctx_t* ctx)
{
    ngx_list_part_t *part  = &(ctx->src->part);
    ngx_worker_path *array = NULL;
    ngx_worker_path *path  = NULL;
    while (part)
    {
        array = (ngx_worker_path*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            path = &array[loop];
            if(NGX_WORKER_PATH_TYPE_DIR == path->type) {
                ngx_media_worker_transfer_upload_dir(ctx,&path->path);
            }
            else if(NGX_WORKER_PATH_TYPE_FILE == path->type) {
                ngx_media_worker_transfer_upload_file(ctx, &path->path);
            }
            else {
                /* TODO:  write warning log */
            }

            /* report the status */
            if(ctx->watcher) {
                ctx->watcher(ngx_media_worker_status_running,NGX_MEDIA_ERROR_CODE_OK,ctx->wk_ctx);
            }
        }
        part = part->next;
    }
    return;
}
static void
ngx_media_worker_transfer_download(ngx_worker_transfer_ctx_t* ctx)
{
    ngx_list_part_t *part  = &(ctx->src->part);
    ngx_worker_path *array = NULL;
    ngx_worker_path *path  = NULL;

    while (part)
    {
        array = (ngx_worker_path*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            path = &array[loop];
            if(NGX_WORKER_PATH_TYPE_FILE == path->type) {
                ngx_media_worker_transfer_download_file(ctx, &path->path);
            }
            else {
                /* TODO:  write warning log */
            }

            /* report the status */
            if(ctx->watcher) {
                ctx->watcher(ngx_media_worker_status_running,NGX_MEDIA_ERROR_CODE_OK,ctx->wk_ctx);
            }
        }
        part = part->next;
    }
    return;
}

static void
ngx_media_worker_transfer_upload_file(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file_path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_file begin,dst:[%d] file:[%V].",
                          ctx->dst_fs,file_path);
    if(NGX_WORKER_STORAGE_TYPE_REMOTE_URL == ctx->dst_fs) {
        /* use libcurl to upload the file */
        ngx_media_worker_transfer_upload_file_standard(ctx,file_path);
    }
    else if(NGX_WORKER_STORAGE_TYPE_LOCAL_FS == ctx->dst_fs) {
        /* local copy the file */
        ngx_media_worker_transfer_local_copy(ctx,file_path);
    }
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_file end.");
    return;
}
static void
ngx_media_worker_transfer_upload_dir(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * dir_path)
{
    ngx_file_info_t           fi;
    ngx_tree_ctx_t            tree;


    if (ngx_link_info(dir_path->data, &fi) == NGX_FILE_ERROR) {
        return;
    }
    if (!ngx_is_dir(&fi)) {
        return;
    }

    tree.init_handler = NULL;
    tree.file_handler = ngx_media_worker_transfer_upload_tree_file;
    tree.pre_tree_handler = ngx_media_worker_transfer_upload_tree_dir;
    tree.post_tree_handler = ngx_media_worker_transfer_upload_tree_noop;
    tree.spec_handler = ngx_media_worker_transfer_upload_tree_noop;
    tree.data = ctx;
    tree.alloc = 0;
    tree.log =ctx->log;

    if (ngx_walk_tree(&tree, dir_path) == NGX_OK) {
        return ;
    }

    return;
}
static ngx_int_t
ngx_media_worker_transfer_upload_tree_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)ctx->data;
    ngx_media_worker_transfer_upload_file(trans_ctx,path);
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_upload_tree_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}
static ngx_int_t
ngx_media_worker_transfer_upload_tree_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_delete_file,delete file:[%V].",path);

    if (ngx_delete_file(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_delete_file,delete file:[%V] fail.",path);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_delete_dir,delete dir:[%V].",path);

    if (ngx_delete_dir(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_upload_delete_file,delete dir:[%V] fail.",path);
    }

    return NGX_OK;

}

static ngx_int_t
ngx_media_worker_transfer_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

static void
ngx_media_worker_transfer_delete(ngx_worker_transfer_ctx_t* ctx)
{
    ngx_list_part_t *part  = &(ctx->delete->part);
    ngx_str_t       *array = NULL;
    ngx_str_t       *path  = NULL;
    ngx_file_info_t  fi;
    ngx_tree_ctx_t   tree;

    /* delete the source dir */
    tree.init_handler = NULL;
    tree.file_handler = ngx_media_worker_transfer_delete_file;
    tree.pre_tree_handler = ngx_media_worker_transfer_delete_noop;
    tree.post_tree_handler = ngx_media_worker_transfer_delete_dir;
    tree.spec_handler = ngx_media_worker_transfer_delete_file;
    tree.data = ctx;
    tree.alloc = 0;
    tree.log =ctx->log;

    while (part)
    {
        array = (ngx_str_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            path = &array[loop];

            if (ngx_link_info(path->data, &fi) == NGX_FILE_ERROR) {
                continue;
            }
            if (ngx_is_dir(&fi)) {
                if (ngx_walk_tree(&tree, path) != NGX_OK) {
                    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                                      "ngx_media_worker_transfer_delete,walk tree:[%V] fail.",path);
                    continue;
                }
                if (ngx_delete_dir(path->data) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                                      "ngx_media_worker_transfer_delete,delete dir:[%V] fail.",path);
                    continue;
                }
            }
            else if(ngx_is_file(&fi)) {
                if (ngx_delete_file(path->data) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                                      "ngx_media_worker_transfer_delete,delete file:[%V] fail.",path);
                }
            }

        }
        part = part->next;
    }
}

static void
ngx_media_worker_transfer_upload_file_standard(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file)
{
    return;
}


static void
ngx_media_worker_transfer_download_file(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file)
{
    if(NGX_WORKER_STORAGE_TYPE_REMOTE_URL == ctx->src_fs) {
        ngx_media_worker_transfer_download_file_standard(ctx,file);
    }
    else if(NGX_WORKER_STORAGE_TYPE_LOCAL_FS == ctx->src_fs) {
        ngx_media_worker_transfer_local_copy(ctx,file);
    }

    return;
}

static size_t
ngx_media_worker_transfer_download_file_standard_fwrite(void *buffer, size_t size, size_t nmemb, void *stream)
{
    ngx_worker_transfer_write_ctx_t* ctx = (ngx_worker_transfer_write_ctx_t* )stream;

    return fwrite(buffer, size, nmemb, ctx->fd);
}

static void
ngx_media_worker_transfer_download_file_standard(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file)
{
    ngx_file_info_t           fi;
    ngx_str_t                 name;
    ngx_str_t                 path;
    CURL                     *curl;
    u_char                    full_path[NGX_HTTP_PATH_LEN+1];
    u_char                   *last = NULL;


    ngx_worker_transfer_write_ctx_t wr_ctx;
    wr_ctx.ret = NGX_OK;

    if( NGX_WORKER_STORAGE_TYPE_LOCAL_FS != ctx->dst.system ) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,the dst is not local file system.");
        return;
    }

    if(NGX_OK != ngx_media_worker_get_file_name(ctx->pool,file,&name)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,get file name fail.");
        return;
    }

    /* get the local dir path*/
    if(NGX_WORKER_PATH_TYPE_FILE == ctx->dst.type) {
        if(NGX_OK != ngx_media_worker_get_file_path(ctx->pool,&ctx->dst.path,&path)) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,get file dir fail.");
            return;
        }
    }
    else
    {
        path = ctx->dst.path;
    }


    if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
        if (ngx_create_dir(path.data, 0700) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,create dir:[%V] fail.",&path);
            return;
        }

        if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "ngx_media_worker_transfer_download_file_standard,get path:[%V] info fail.",&path);
            return;
        }
    }
    if (!ngx_is_dir(&fi)) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,dst:[%V] is not a dir.",&path);
        return;
    }

    ngx_memzero(&full_path, NGX_HTTP_PATH_LEN+1);

    if(NGX_WORKER_PATH_TYPE_FILE == ctx->dst.type) {
        last = ngx_snprintf(&full_path[0], NGX_HTTP_PATH_LEN,"%V", &ctx->dst.path);
    }
    else
    {
        last = ngx_snprintf(&full_path[0], NGX_HTTP_PATH_LEN,"%V/%V", &path,&name);
    }
    *last = '\0';

    /* open the file for write */
    wr_ctx.fd = fopen((char*)&full_path[0], "wb");
    if ( NULL == wr_ctx.fd) {
        ngx_log_error(NGX_LOG_EMERG, ctx->log, ngx_errno, "open the file:[%V] failed", &path);
        return ;
    }

    curl = curl_easy_init();

    if(curl) {
         ngx_str_t authInfo;
         ngx_str_t url;

         ngx_str_null(&authInfo);
         ngx_str_null(&url);

         if( NGX_OK != ngx_media_worker_parse_url(ctx->pool,file,&url,&authInfo)) {
             curl_easy_setopt(curl, CURLOPT_URL,file->data);
         }
         else {
             curl_easy_setopt(curl, CURLOPT_URL,url.data);
             curl_easy_setopt(curl, CURLOPT_USERPWD, authInfo.data);
         }

         curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ngx_media_worker_transfer_download_file_standard_fwrite);
         curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wr_ctx);
         CURLcode cur_ret = curl_easy_perform(curl);
         if(CURLE_OK  != cur_ret) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_download_file_standard,"
                          "curl perform fail,ret:[%d] file:[%V] url:[%V] auth:[%V]."
                          "reason:[%s].",
                           cur_ret,file,&url,&authInfo,
                           curl_easy_strerror(cur_ret));
         }

         curl_easy_cleanup(curl);
    }

    fflush(wr_ctx.fd);
    fclose(wr_ctx.fd);

    return;
}

static void
ngx_media_worker_transfer_local_copy(ngx_worker_transfer_ctx_t* ctx,ngx_str_t * file)
{
    return;
}




static ngx_int_t
ngx_media_worker_transfer_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_int_t ret = NGX_OK;

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_init begin.");

    /* create the cut context */
    ngx_uint_t lens = sizeof(ngx_worker_transfer_ctx_t);
    ngx_worker_transfer_ctx_t *trans_ctx = ngx_pcalloc(ctx->pool,lens);

    if(NULL == trans_ctx) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_init allocate transfer ctx fail.");
        return NGX_ERROR;
    }

    ngx_media_worker_unescape_uri(ctx);

    ctx->priv_data_size   = lens;
    ctx->priv_data        = trans_ctx;

    trans_ctx->pool       = ctx->pool;
    trans_ctx->log        = ctx->log;
    trans_ctx->watcher    = watch;
    trans_ctx->wk_ctx     = ctx;
    trans_ctx->running    = 0;

    trans_ctx->transType  = NGX_WORKER_TRANS_TYPE_UPLOAD;
    trans_ctx->dst.type   = NGX_WORKER_PATH_TYPE_FILE;
    ngx_str_null(&trans_ctx->dst.path);
    trans_ctx->src        = ngx_list_create(ctx->pool,NGX_WORKER_TRANS_MAX,sizeof(ngx_worker_path));
    if(NULL == trans_ctx->src) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_init allocate transfer src path fail.");
        return NGX_ERROR;
    }

    trans_ctx->delete     = ngx_list_create(ctx->pool,NGX_WORKER_TRANS_MAX,sizeof(ngx_str_t));
    if(NULL == trans_ctx->src) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_init allocate transfer src path fail.");
        return NGX_ERROR;
    }

    /* init the curl lib */
    curl_global_init(CURL_GLOBAL_DEFAULT);


    /* parser the args */
    ret = ngx_media_worker_transfer_parser_agrs(ctx);
    if(NGX_OK != ret) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_init parser the args fail.");
        return NGX_ERROR;
    }
    /* check the args */
    ret = ngx_media_worker_transfer_check_agrs(ctx);
    if(NGX_OK != ret) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_transfer_init check the args fail.");
        return NGX_ERROR;
    }
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_init end.");
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_release(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_release begin.");
    curl_global_cleanup();
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_release end.");
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_start(ngx_media_worker_ctx_t* ctx)
{
    int             err;
    pthread_attr_t  attr;

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_start worker:[%V] begin.",&ctx->wokerid);

    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)ctx->priv_data;
    /* start the worker thread */
    err = pthread_attr_init(&attr);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, ctx->log, err,
                      "pthread_attr_init() failed");
        return NGX_ERROR;
    }

    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, ctx->log, err,
                      "pthread_attr_setdetachstate() failed");
        return NGX_ERROR;
    }
    trans_ctx->running = 1;
    err = pthread_create(&trans_ctx->threadid, &attr, ngx_media_worker_transfer_thread, trans_ctx);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, ctx->log, err,
                      "pthread_create() failed");
        return NGX_ERROR;
    }
    ctx ->status = ngx_media_worker_status_start;
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_start worker:[%V] end.",&ctx->wokerid);
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_transfer_stop(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_stop begin.");
    ngx_worker_transfer_ctx_t* trans_ctx = (ngx_worker_transfer_ctx_t*)ctx->priv_data;
    trans_ctx->running = 0;
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_transfer_stop end.");
    return NGX_OK;
}


/* video file cut worker */
ngx_media_worker_t ngx_video_transfer_worker = {
    .type             = ngx_media_worker_tranfer,
    .init_worker      = ngx_media_worker_transfer_init,
    .release_worker   = ngx_media_worker_transfer_release,
    .start_worker     = ngx_media_worker_transfer_start,
    .stop_worker      = ngx_media_worker_transfer_stop,
    .control_worker   = NULL,
};


