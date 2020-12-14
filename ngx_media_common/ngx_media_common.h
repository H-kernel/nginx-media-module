#ifndef _NGX_MEDIA_COMMON_H_INCLUDED_
#define _NGX_MEDIA_COMMON_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

ngx_int_t ngx_media_mkdir_full_path(u_char *dir, ngx_uint_t access);

#endif /*_NGX_MEDIA_COMMON_H_INCLUDED_ */