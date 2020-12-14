
#ifndef __NGX_SHMAP_H__
#define __NGX_SHMAP_H__

#include <ngx_config.h>
#include <ngx_core.h>
#include <stdint.h>

#define VT_BINARY 0
#define VT_INT32 1
#define VT_INT64 2
#define VT_DOUBLE 3
#define VT_STRING 4
#define VT_NULL 5

#pragma pack(push) //�������״̬
#pragma pack(4) //����4�ֽڶ���
typedef struct {
    u_char                       color;
    u_char                       dummy;
    u_short                      key_len;
    ngx_queue_t                  queue;
    uint64_t                     expires;  //���ڵľ���ʱ��(����)(���������е�exptime������)
    uint32_t                     value_len;
    uint32_t                     user_flags;
    uint8_t                      value_type;
    u_char                       data[1];
} ngx_shmap_node_t;
#pragma pack(pop) //�ָ�����״̬��

typedef struct {
    ngx_rbtree_t                  rbtree;
    ngx_rbtree_node_t             sentinel;
    ngx_queue_t                   queue;
} ngx_shmap_shctx_t;


typedef struct {
    ngx_shmap_shctx_t  *sh;
    ngx_slab_pool_t              *shpool;
    ngx_str_t                     name;
    ngx_log_t                    *log;
} ngx_shmap_ctx_t;

/**
 * ��ʼ�������ڴ��ֵ�
 **/
ngx_shm_zone_t* ngx_shmap_init(ngx_conf_t *cf, ngx_str_t* name, size_t size, void* module);

/**
 * ȡ��һ��key��ֵ��
 * key Ϊ�ֵ��key.
 * data Ϊȡ�õ�����(ȡ�õ�������ֱ��ָ�����ڴ����ģ�
 *          ����������޸��˸����ݣ������ڴ��е�����Ҳ�ᱻ�޸�)
 * value_type Ϊ���ݵ�����
 * exptime Ϊ���ж�ù���(��)
 * user_flags �������õ�user_flagsֵ
 **/
int ngx_shmap_get(ngx_shm_zone_t* zone, ngx_str_t* key, 
		ngx_str_t* data, uint8_t* value_type,uint32_t* exptime,uint32_t* user_flags);

/**
 * ��ngx_shmap_get��ͬ��ȡ��һ��key��ֵ��
 * ��ngx_shmap_get��֮ͬ������user_flags���ص������õ�
 *    user_flags��ָ�룬�����ڻ�ȡ���user_flags�����޸ġ�
 **/
int ngx_shmap_get_ex(ngx_shm_zone_t* zone, ngx_str_t* key, 
		ngx_str_t* data, uint8_t* value_type,uint32_t* exptime,uint32_t** user_flags);

int ngx_shmap_get_int32(ngx_shm_zone_t* zone, ngx_str_t* key, int32_t* i);
int ngx_shmap_get_int64(ngx_shm_zone_t* zone, ngx_str_t* key, int64_t* i);
int ngx_shmap_get_int64_and_clear(ngx_shm_zone_t* zone, 
				ngx_str_t* key, int64_t* i);
// ɾ��һ��KEY
int ngx_shmap_delete(ngx_shm_zone_t* zone, ngx_str_t* key);

typedef void (*foreach_pt)(ngx_shmap_node_t* node, void* extarg);

/**
 * ѭ��������ֵ�
 */
int ngx_shmap_foreach(ngx_shm_zone_t* zone, foreach_pt func, void* args);

//��������ֵ�
int ngx_shmap_flush_all(ngx_shm_zone_t* zone);
//��չ��ڵ�key
int ngx_shmap_flush_expired(ngx_shm_zone_t* zone, int attempts);
//���һ��key,value, ������ڻᱨ��(�ռ䲻��ʱ����ɾ��������ڵ�����)
int ngx_shmap_add(ngx_shm_zone_t* zone, ngx_str_t* key, ngx_str_t* value,
			uint8_t value_type, uint32_t exptime, uint32_t user_flags);
//���һ��key,value, ������ڻᱨ��(�ռ䲻��ʱ���᷵��ʧ��)
int ngx_shmap_safe_add(ngx_shm_zone_t* zone, ngx_str_t* key, ngx_str_t* value,
			uint8_t value_type, uint32_t exptime, uint32_t user_flags);
//�滻һ��key,value
int ngx_shmap_replace(ngx_shm_zone_t* zone, ngx_str_t* key, ngx_str_t* value,
			uint8_t value_type, uint32_t exptime, uint32_t user_flags);
//����һ��key,value.
int ngx_shmap_set(ngx_shm_zone_t* zone, ngx_str_t* key, ngx_str_t* value,
			uint8_t value_type, uint32_t exptime, uint32_t user_flags);
int ngx_shmap_safe_set(ngx_shm_zone_t* zone, ngx_str_t* key, ngx_str_t* value,
			uint8_t value_type, uint32_t exptime, uint32_t user_flags);

//��key����i,���������Ӻ��ֵ��
int ngx_shmap_inc_int(ngx_shm_zone_t* zone, ngx_str_t* key,int64_t i,uint32_t exptime, int64_t* ret);
//��key����d,���������Ӻ��ֵ��
int ngx_shmap_inc_double(ngx_shm_zone_t* zone, ngx_str_t* key,double d,uint32_t exptime,double* ret);

void ngx_str_set_int32(ngx_str_t* key, int32_t* ikey);
void ngx_str_set_int64(ngx_str_t* key, int64_t* ikey);
void ngx_str_set_double(ngx_str_t* key, double* value);
uint32_t ngx_shmap_crc32(u_char *p, size_t len);

#endif

