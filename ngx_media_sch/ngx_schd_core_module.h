#ifndef _NGX_SCHD_CORE_H_INCLUDED_
#define _NGX_SCHD_CORE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
#include "ngx_media_common.h"
#include <common/as_json.h>

ngx_int_t ngx_schd_zk_get_system_info_json(cJSON *root);
ngx_int_t ngx_schd_zk_get_system_info_xml(xmlDocPtr root);
#endif /* _NGX_SCHD_CORE_H_INCLUDED_*/