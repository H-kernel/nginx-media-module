/*!
 * \file conn_pool.c
 * \author Nathan Eloe
 * \brief The zmq connection connection pool function definitions
 */

#include "ngx_zmq_conn_pool.h"
#define DEBUG 0
ngx_zmq_conn_pool* ngx_zmq_init_pool( void* ctx, ngx_pool_t* mpool, int stype) 
{
  ngx_zmq_conn_pool * p = ngx_pcalloc(mpool, sizeof(ngx_zmq_conn_pool));
  p->m_ctx = ctx;
  p->m_stype = stype;
  p->m_to = -2;
  /* not going to set up a connection yet, that will happen later */
  return p;
}

void ngx_zmq_set_endpt(ngx_zmq_conn_pool* cp, ngx_str_t endpt )
{
  if (!cp->m_endpt)
  {
    cp -> m_endpt = calloc(endpt.len + 1, 1);
    ngx_memcpy(cp->m_endpt, endpt.data, endpt.len);
#if DEBUG
    fprintf(stderr,"Setting endpoint to: %s\n", cp->m_endpt);
#endif
  }
  return;
}

void ngx_zmq_set_socktype(ngx_zmq_conn_pool* cp, const int stype)
{
  cp->m_stype = stype;
  return;
}


ngx_zmq_conn* ngx_zmq_init_conn(ngx_zmq_conn_pool* cp)
{
#if DEBUG
  fprintf(stderr, "making new connection\n");
#endif
  ngx_zmq_conn * c = malloc(sizeof(ngx_zmq_conn));
  c->m_sock = zmq_socket(cp->m_ctx, cp->m_stype);
  zmq_connect(c->m_sock, cp->m_endpt);
  return c;
}

ngx_zmq_conn* ngx_zmq_get_conn(ngx_zmq_conn_pool* cp )
{
#if DEBUG
  fprintf(stderr, "getting a connection\n");
#endif
  ngx_zmq_conn* c;
  if (cp->m_front)
  {
    c = cp->m_front;
    if (cp->m_front == cp->m_back)
      cp->m_back = NULL;
    cp->m_front = cp->m_front->m_next;
  }
  else
    c = ngx_zmq_init_conn(cp);
  return c;
}

void ngx_zmq_rel_conn(ngx_zmq_conn_pool* cp, ngx_zmq_conn** con )
{
#if DEBUG
  fprintf(stderr, "releasing connection to pool\n");
#endif
  if (cp->m_back)
    cp->m_back = cp->m_back->m_next = *con;
  else
    cp->m_front = cp->m_back = *con;
  *con = NULL;
  return;
}

void ngx_zmq_free_conn(ngx_zmq_conn** con ) 
{
#if DEBUG
  fprintf(stderr, "destroying connection\n");
#endif
  int time = 0;
  if (!(*con))
    return;
  if ((*con)->m_sock)
  {
    zmq_setsockopt((*con)->m_sock, ZMQ_LINGER, &time, sizeof(time));
    zmq_close((*con)->m_sock);
  }
  free(*con);
  (*con) = NULL;
  return;
}

void ngx_zmq_free_pool(ngx_zmq_conn_pool** cp )
{
  if (!(*cp))
    return;
  ngx_zmq_conn * it = (*cp)->m_front;
  ngx_zmq_conn * i = it;
  while (i)
  {
    it = it->m_next;
    ngx_zmq_free_conn(&i);
    i = it;
  }
  /*I do not want to muck with the memory pools or the context*/
  if ((*cp)->m_endpt)
    free((*cp)->m_endpt);
  (*cp)->m_ctx = NULL;
  ngx_free(*cp);
  *cp = NULL;
  return;
}

