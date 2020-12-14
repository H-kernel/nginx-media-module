/* 
 * Copyright clickmeeting.com 
 * Wojtek Kosak <wkosak@gmail.com>
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

#include "ngx_rtmpt_proxy_session.h"
#include "ngx_rtmpt_proxy_module.h"
#include "ngx_rtmpt_proxy_transport.h"
 
#define RTMPT_HASHMAX 1000

ngx_rtmpt_proxy_session_t  *ngx_rtmpt_proxy_sessions_global[RTMPT_HASHMAX];

char ngx_rtmpt_proxy_intervals_def[]={0,0x01, 0x03, 0x05, 0x09, 0x11, 0x21, 0x41,0};
//char ngx_rtmpt_proxy_intervals_def[]={0,0x01,0};


ngx_rtmpt_proxy_session_t **ngx_rtmpt_proxy_session_getall(ngx_uint_t *hs) {
	*hs=RTMPT_HASHMAX;
	return ngx_rtmpt_proxy_sessions_global;
}

static ngx_int_t
ngx_rtmpt_proxy_session_get_peer(ngx_peer_connection_t *pc, void *data)
{
    return NGX_OK;
}

    
static void
ngx_rtmpt_proxy_session_free_peer(ngx_peer_connection_t *pc, void *data,
            ngx_uint_t state)
{
}

static void session_name_create(u_char *buf, int size) {
   int i;
   for (i=0;i<size;i++) {
     buf[i]=(rand()%2?((rand()%2?'a':'A')+rand()%26):'0'+rand()%10);
   }
}


static ngx_rtmpt_proxy_session_t *get_session_from_hash(u_char *name, ngx_uint_t size) {

   ngx_rtmpt_proxy_session_t    **sessions, *ts; 
   
   int hash=ngx_hash_key(name,size)%RTMPT_HASHMAX;

   sessions = ngx_rtmpt_proxy_sessions_global; 
   
  
   for (ts=sessions[hash];ts;ts=ts->next) {
     if (ts->name.len==size && ngx_strncmp(ts->name.data,name,size)==0) {
        return ts;
     }
   }
   return NULL;
}

static void put_session_in_hash(ngx_rtmpt_proxy_session_t *session) {

   ngx_rtmpt_proxy_session_t    **sessions, *ts=NULL, *cs=NULL;
   
   int hash=ngx_hash_key(session->name.data,session->name.len)%RTMPT_HASHMAX;
   
   sessions = ngx_rtmpt_proxy_sessions_global; 
	
   if (!sessions[hash]) {
      sessions[hash]=session;
      return;
   }
   for (ts=sessions[hash];ts;ts=ts->next) {
     cs=ts;
   }
   
   cs->next=session;
   session->prev=cs;
}

static void remove_session_from_hash(u_char *name, ngx_uint_t size) {
    ngx_rtmpt_proxy_session_t    **sessions, *ts=NULL;
	
    int hash=ngx_hash_key(name,size)%RTMPT_HASHMAX;
	
	sessions = ngx_rtmpt_proxy_sessions_global; 
	
    for (ts=sessions[hash];ts;ts=ts->next) {
     if (ts->name.len==size && ngx_strncmp(ts->name.data,name,size)==0) {
        
        if (ts->next) {
           ts->next->prev=ts->prev;
        }
        if (ts->prev) {
           ts->prev->next=ts->next;
        }
        if (ts == sessions[hash]) {
           sessions[hash]=ts->next;
        }
        return;
     }
   }
   return;
}



ngx_rtmpt_proxy_session_t 
	*ngx_rtmpt_proxy_create_session(ngx_http_request_t *r) 
{
	ngx_rtmpt_proxy_session_t 		*session;
	ngx_peer_connection_t     		*pc;
	ngx_rtmpt_proxy_loc_conf_t  	*plcf;
	ngx_pool_t                     	*pool = NULL;
	ngx_url_t                   	url;
	int 							rc;
	
	
	plcf = ngx_http_get_module_loc_conf(r, ngx_rtmpt_proxy_module);
	
	if (!plcf) {
		goto error;
	}
	
	
	pool = ngx_create_pool(4096, plcf->log);
	if (pool == NULL) {
		ngx_log_error(NGX_LOG_ERR, plcf->log, 0, "rtmpt/session: cannot create pool for session");
		goto error;
	}
	
	
	session = (ngx_rtmpt_proxy_session_t *) ngx_pcalloc(pool, sizeof(ngx_rtmpt_proxy_session_t));
	if (session == NULL) {
		ngx_log_error(NGX_LOG_ERR, plcf->log, 0, "rtmpt/session: cannot allocate memory for session");
		goto error;
	}
	
	session->name.data=NULL;
	session->name.len=0;
	
	ngx_str_set(&session->name, "1234567890123456");
	session->name.data = ngx_pstrdup(pool, &session->name);
	session_name_create(session->name.data,session->name.len);
		 
	session->log = plcf->log;
	session->pool = pool;
	session->sequence = 0;   
	session->on_finish_send = NULL;
	session->chain_from_http_request = NULL;   
	session->chain_from_nginx = NULL;
	session->out_pool = NULL;
	session->interval_check_time=0;
	session->interval_check_att=0;
	session->interval_check_count=0;
	session->interval_position=1;
	session->created_at = ngx_cached_time->sec;
	session->http_requests_count = 0;
	session->bytes_from_http = session->bytes_to_http = 0;
	
	session->create_request_ip.data=ngx_pstrdup(pool,&r->connection->addr_text);
	session->create_request_ip.len=r->connection->addr_text.len;
	
        bzero(session->waiting_requests,NGX_RTMPT_PROXY_REQUESTS_DELAY_SIZE*sizeof(ngx_http_request_t *));
        session->in_process = 0;

	put_session_in_hash(session);
	
	pc = ngx_pcalloc(pool, sizeof(ngx_peer_connection_t));
	if (pc == NULL) {
		ngx_log_error(NGX_LOG_ERR, plcf->log, 0, "rtmpt/session: cannot allocate for peer connection");
		goto error;
	} 
	
	ngx_memzero(&url, sizeof(ngx_url_t));
	url.url.data = plcf->target.data;
	url.url.len = plcf->target.len;
	url.default_port = 1935;
	url.uri_part = 1;
	
	if (ngx_parse_url(pool, &url) != NGX_OK) {
		ngx_log_error(NGX_LOG_ERR, plcf->log, 0, "rtmpt/session: error [%s] failed to parse server name: %V", url.err, &url.url);
		goto error;
	}
	
	
	session->target_url.data=ngx_pstrdup(pool,&url.url);
	session->target_url.len=url.url.len;
	
	ngx_memzero(pc, sizeof(ngx_peer_connection_t));
	pc->log = session->log;
    pc->get = ngx_rtmpt_proxy_session_get_peer;
    pc->free = ngx_rtmpt_proxy_session_free_peer;
	
	pc->sockaddr = url.addrs[0].sockaddr;
	pc->socklen = url.addrs[0].socklen;
	pc->name = &url.addrs[0].name;
    

    rc = ngx_event_connect_peer(pc);
	if (rc != NGX_OK && rc != NGX_AGAIN ) {
		ngx_log_error(NGX_LOG_ERR, plcf->log, 0, "rtmpt/session: error in connect peer");
		goto error;
	}

	pc->connection->data = session;
	pc->connection->read->handler = ngx_rtmpt_read_from_rtmp;
	pc->connection->write->handler = ngx_rtmpt_send_chain_to_rtmp; 
	pc->connection->idle = 0;
	pc->connection->log = session->log;
	pc->connection->pool = session->pool;
	pc->connection->pool->log = session->log;
	pc->connection->read->log = session->log;
	pc->connection->write->log = session->log;
	
	
	
	
	session->connection = pc->connection;
	
   	return session;
	
error:
	if (pool) {
		ngx_destroy_pool(pool);
	}
	return NULL;
}


ngx_rtmpt_proxy_session_t 
	*ngx_rtmpt_proxy_get_session(ngx_str_t *id) 
{
	return get_session_from_hash(id->data,id->len);
}


void 
	ngx_rtmpt_proxy_destroy_session(ngx_rtmpt_proxy_session_t *session) 
{
	int i;

	remove_session_from_hash(session->name.data, session->name.len);

	for (i=0;i<NGX_RTMPT_PROXY_REQUESTS_DELAY_SIZE;i++) {
		if (session->waiting_requests[i]) {
			ngx_http_request_t *r;
			ngx_log_error(NGX_LOG_ERR, session->log, 0, "rtmpt/session: finalize waiting request with 404 %V", &session->waiting_requests[i]->uri);
			r=session->waiting_requests[i];
			session->waiting_requests[i]=NULL;
			ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
		}

	}
	if (session->connection) {
		ngx_close_connection(session->connection);
		session->connection = NULL;
	}

	if (session->out_pool) {
		ngx_destroy_pool(session->out_pool);
		session->out_pool = NULL;
		session->chain_from_nginx = NULL;
	}
	if (session->pool) {
		ngx_destroy_pool(session->pool);
		session->pool = NULL;
	}
}


