#include "ngx_media_common.h"
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>

ngx_int_t
ngx_media_mkdir_full_path(u_char *dir, ngx_uint_t access)
{
    u_char     *p, ch;
    ngx_err_t   err;

    err = 0;

#if (NGX_WIN32)
    p = dir + 3;
#else
    p = dir + 1;
#endif

    for ( /* void */ ; *p; p++) {
        ch = *p;

        if (ch != '/') {
            continue;
        }

        *p = '\0';
        if (ngx_create_dir(dir, access) == NGX_FILE_ERROR) {
            err = ngx_errno;

            switch (err) {
            case NGX_EEXIST:
                err = 0;
                break;
            case NGX_EACCES:
                break;
            default:
                return err;
            }
        }

        *p = '/';
    }

    if (ngx_create_dir(dir, access) == NGX_FILE_ERROR) {
        err = ngx_errno;

        switch (err) {
        case NGX_EEXIST:
            err = 0;
            break;
        case NGX_EACCES:
            break;
        default:
            return err;
        }
    }

    return err;
}