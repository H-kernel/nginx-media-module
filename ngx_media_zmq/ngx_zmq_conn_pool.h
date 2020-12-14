/*!
 * \file conn_pool.h
 * \author Nathan Eloe
 * \brief A ZMQ connection pool
 */

#ifndef CONN_POOL_H
#define CONN_POOL_H
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <zmq.h>

typedef struct _ngx_zmq_conn
{
  struct _ngx_zmq_conn *m_next;
  void                 *m_sock;
} ngx_zmq_conn;

typedef struct
{
  char *m_endpt;
  void *m_ctx;
  int   m_stype;
  int   m_to;
  ngx_zmq_conn *m_front, *m_back;
} ngx_zmq_conn_pool;

ngx_zmq_conn_pool* ngx_zmq_init_pool(void* ctx, ngx_pool_t* mpool, int stype );
void               ngx_zmq_set_endpt(ngx_zmq_conn_pool* cp, ngx_str_t endpt );
void               ngx_zmq_set_socktype(ngx_zmq_conn_pool* cp, const int stype);
ngx_zmq_conn*      ngx_zmq_init_conn(ngx_zmq_conn_pool* cp);
ngx_zmq_conn*      ngx_zmq_get_conn(ngx_zmq_conn_pool* cp);
void               ngx_zmq_rel_conn(ngx_zmq_conn_pool* cp, ngx_zmq_conn** con);
void               ngx_zmq_free_conn(ngx_zmq_conn** con);
void               ngx_zmq_free_pool(ngx_zmq_conn_pool** cp);

#endif