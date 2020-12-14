#include "ngx_media_sys_stat.h"
#include "ngx_media_include.h"

#define NGX_STAT_INTERVAL_NUM   3

#define NGX_STAT_DEFAULT_INTERVAL   5

#define NGX_STAT_NETCARD_INFO_LEN     64
#define MAX_NETCARD_NUM    16
#define MAX_DISK_PATH_NUM  64
#define IP_STRING_LEN           64



#define STAT_PATH_LEN 128

#define NGX_BANDWIDTH_DEFAULT        1000*1024
#define ABNOMAL_VALUE 0xFFFF
#define BOND_MODE_ACTIVEBACKUP 1
#define BOND_STATE_ACTIVE 0   /* link is active */

#define MAX_NAME_LEN   256

#define DISK_UNIT_MB   1048576

typedef struct
{
    in_addr_t        ip;
    u_char           bondName[NGX_STAT_NETCARD_INFO_LEN];
    ngx_uint_t       mode;
    ngx_uint_t       slaveNum;
    u_char           slaveName[MAX_NETCARD_NUM][NGX_STAT_NETCARD_INFO_LEN];
    u_char           slaveStatus[MAX_NETCARD_NUM];
} BondInfo;



typedef struct tagSysInfo
{
    ngx_uint_t    m_ulCpuUsed[NGX_STAT_INTERVAL_NUM + 1];
    ngx_uint_t    m_ulMemTotal; // MB
    ngx_uint_t    m_ulMemUsed[NGX_STAT_INTERVAL_NUM + 1];  // MB
}SysInfo;

typedef struct tagNetworkCardInfo
{
    u_char m_strIP[IP_STRING_LEN + 1];
    u_char m_strName[NGX_STAT_NETCARD_INFO_LEN + 1];
    ngx_uint_t m_ulBWTotal; //Mbps
    ngx_uint_t m_ullCurrTxByteRecv;
    ngx_uint_t m_ullCurrTxByteSend;
    ngx_uint_t m_ulBWUsedRecv[NGX_STAT_INTERVAL_NUM + 1];//Mbps
    ngx_uint_t m_ulBWUsedSend[NGX_STAT_INTERVAL_NUM + 1];//Mbps
}NetworkCardInfo;

typedef enum
{
    DISK_LOCAL_PATH = 0,
    DISK_MOUNT      = 1 // mount
}DISK_MOUNT_FLAG;

typedef struct tagDiskInfo
{
    char       m_strDiskName[STAT_PATH_LEN + 1];
    char       m_strDiskPath[STAT_PATH_LEN + 1];
    ngx_uint_t m_ullTotalSize;
    ngx_uint_t m_ullUsedSize;
    ngx_uint_t m_nMountFlag;
}DiskInfo;

typedef struct tagVideoSysStat
{
    pthread_t             SysStatthread;
    ngx_thread_mutex_t    StatMutex;
    ngx_pool_t           *pool;
    ngx_log_t            *log;
	SysInfo               sysInfo;
    volatile ngx_uint_t   ulStatIndex;
	volatile ngx_uint_t   ulRunning;
	volatile ngx_uint_t   bIsMountsOk;
    char                  strMountsInfo[STAT_PATH_LEN * 32 * 64];
	ngx_uint_t            DiskCount;
	ngx_uint_t            NetWorkCount;
    DiskInfo            **DiskInfoList;
    NetworkCardInfo	    **NetWorkInfoList;
}VideoSysStat;

static VideoSysStat* g_VideoSysStat = NULL;


static void*     ngx_media_sys_stat_svc(void *data);
static ngx_int_t ngx_media_sys_stat_stat_cpuinfo();
static ngx_int_t ngx_media_sys_stat_stat_memoryinfo();
static ngx_int_t ngx_media_sys_stat_stat_diskinfo();
static ngx_int_t ngx_media_sys_stat_stat_networkcard_info();
static void      ngx_media_sys_stat_read_mountfile();
static ngx_int_t ngx_media_sys_stat_check_diskmount_state(u_char * strDiskPath);
static void      ngx_media_sys_stat_stat_netcard_info(NetworkCardInfo* pNetworkCard);
static ngx_int_t ngx_media_sys_stat_stat_bondnetcardinfo(int32_t isock, NetworkCardInfo* pNetworkCard, struct ifreq* pIfr );
static ngx_int_t ngx_media_sys_stat_stat_ethnetcardinfo(int32_t isock, NetworkCardInfo* pNetworkCard, struct ifreq* pIfr );
static void      ngx_media_sys_stat_stat_BWinfo(NetworkCardInfo* pNetworkCard);





ngx_int_t
ngx_media_sys_stat_init(ngx_cycle_t *cycle)
{
    int             err;
    pthread_attr_t  attr;

    if(g_VideoSysStat)
    {
        return NGX_OK;
    }

    g_VideoSysStat = ngx_pcalloc(cycle->pool,sizeof(VideoSysStat));
    g_VideoSysStat->pool = cycle->pool;
    g_VideoSysStat->log  = cycle->log;

    if (ngx_thread_mutex_create(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, g_VideoSysStat->log, 0, "create stat mutex fail.");
        return NGX_ERROR;
    }

    g_VideoSysStat->NetWorkInfoList
                    = ngx_pcalloc(g_VideoSysStat->pool,sizeof(NetworkCardInfo*)*MAX_NETCARD_NUM);
    g_VideoSysStat->DiskInfoList
                    = ngx_pcalloc(g_VideoSysStat->pool,sizeof(DiskInfo*)*MAX_DISK_PATH_NUM);


    g_VideoSysStat->ulRunning = 1;

    err = pthread_attr_init(&attr);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, g_VideoSysStat->log, err,
                      "pthread_attr_init() failed");
        g_VideoSysStat->ulRunning = 0;
        return NGX_ERROR;
    }

    err = pthread_create(&g_VideoSysStat->SysStatthread, &attr, ngx_media_sys_stat_svc, g_VideoSysStat);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, g_VideoSysStat->log, err,
                          "pthread_create() failed");
        g_VideoSysStat->ulRunning = 0;
        return NGX_ERROR;
    }

    (void) pthread_attr_destroy(&attr);

    return NGX_OK;
}

void
ngx_media_sys_stat_release(ngx_cycle_t *cycle)
{
    if(NULL == g_VideoSysStat)
    {
        return;
    }
    if(0 == g_VideoSysStat->ulRunning)
    {
        return;
    }
    g_VideoSysStat->ulRunning = 0;
    (void)ngx_thread_mutex_destroy(&g_VideoSysStat->StatMutex,cycle->log);
}


static void*
ngx_media_sys_stat_svc(void *data)
{
    ngx_uint_t i = 0;
    VideoSysStat* SysStat = (VideoSysStat*)data;
    while (SysStat->ulRunning)
    {
        for (i = 0; i < NGX_STAT_DEFAULT_INTERVAL; i++)
        {
            if (!SysStat->ulRunning)
            {
                ngx_log_error(NGX_LOG_EMERG, SysStat->log, 0, "the stat thread exit.");
                return NULL;
            }

            if(0 == (i) % NGX_STAT_DEFAULT_INTERVAL)
            {
                (void)ngx_media_sys_stat_stat_cpuinfo();

                (void)ngx_media_sys_stat_stat_memoryinfo();

                (void)ngx_media_sys_stat_stat_diskinfo();

                (void)ngx_media_sys_stat_stat_networkcard_info();

                ++SysStat->ulStatIndex;

                SysStat->ulStatIndex %= NGX_STAT_INTERVAL_NUM;
            }
            ngx_sleep(1);
        }
    }

    ngx_log_error(NGX_LOG_EMERG, SysStat->log, 0, "The system stat thread exit.");
    return NULL;
}

ngx_int_t
ngx_media_sys_stat_add_networdcard(u_char* strIP)
{
    ngx_uint_t                 i = 0;
    NetworkCardInfo* netcardInfo = NULL;
    ngx_int_t          bFindFlag = 0;
    u_char                 *last = NULL;
    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }
    if(NULL == strIP)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "Add a netword card to moudle stat failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->NetWorkCount; i++)
    {
        netcardInfo = g_VideoSysStat->NetWorkInfoList[i];
        if(0 == ngx_strncmp(netcardInfo->m_strIP, strIP, ngx_strlen(strIP)))
        {
            bFindFlag = 1;
            break;
        }
    }

    if(bFindFlag)
    {
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_OK;
        }
        ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,
                       "Add a netword card to stat moudle OK.It already has been added to the list.");

        return NGX_OK;
    }

    netcardInfo = ngx_pcalloc(g_VideoSysStat->pool,sizeof(NetworkCardInfo));

    if(NULL == netcardInfo) {
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_ERROR;
        }
        ngx_log_error(NGX_LOG_EMERG, g_VideoSysStat->log, 0, "create the network info fail.");
        return NGX_ERROR;
    }

    last = ngx_cpymem(&netcardInfo->m_strIP[0], strIP, ngx_strlen(strIP));
    *last = '\0';
    g_VideoSysStat->NetWorkInfoList[g_VideoSysStat->NetWorkCount] = netcardInfo;
    g_VideoSysStat->NetWorkCount++;
    ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,
                       "Add a netword card :[%s]to stat moudle OK.",strIP);
    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }
    return NGX_OK;
}

ngx_int_t
ngx_media_sys_stat_remove_networdcard(u_char* strIP)
{
    ngx_uint_t                 i = 0;
    NetworkCardInfo* netcardInfo = NULL;
    ngx_int_t          bFindFlag = 0;

    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }

    if(NULL == strIP)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "Remove a netword card from stat moudle failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->NetWorkCount; i++)
    {
        netcardInfo = g_VideoSysStat->NetWorkInfoList[i];
        if(0 == ngx_strncmp(netcardInfo->m_strIP, strIP, ngx_strlen(strIP)))
        {
            bFindFlag = 1;
        }
    }

    if(!bFindFlag) {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "Remove a netword card from stat moudle failed as can't find the IP mark.");
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_ERROR;
        }
        return NGX_ERROR;
    }

    for(; i < g_VideoSysStat->NetWorkCount; i++)
    {
        if((i+1) == g_VideoSysStat->NetWorkCount) {
            g_VideoSysStat->NetWorkInfoList[i] = NULL;
            g_VideoSysStat->NetWorkCount--;
            break;
        }
        g_VideoSysStat->NetWorkInfoList[i]
                          = g_VideoSysStat->NetWorkInfoList[i+1];
    }

    (void)ngx_pfree(g_VideoSysStat->pool,netcardInfo);
    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }

    return NGX_OK;
}


ngx_int_t
ngx_media_sys_stat_add_disk(u_char* strDiskName,u_char* strDiskPath)
{
    ngx_uint_t                 i = 0;
    DiskInfo           *diskInfo = NULL;
    ngx_int_t          bFindFlag = 0;
    u_char                 *last = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }
    if(NULL == strDiskPath)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "Add a disk to moudle stat failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];
        if(0 == ngx_strncmp(diskInfo->m_strDiskPath, strDiskPath, ngx_strlen(strDiskPath)))
        {
            bFindFlag = 1;
            break;
        }
    }

    if(bFindFlag)
    {
        ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,
                      "Add a disk to stat moudle OK.It already has been added to the list.");
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_OK;
        }
        return NGX_OK;
    }

    diskInfo = ngx_pcalloc(g_VideoSysStat->pool,sizeof(DiskInfo));
    if(NULL == diskInfo)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0, "Create disk info fail.");
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_ERROR;
        }
        return NGX_ERROR;
    }

    last = ngx_cpymem((u_char *)&diskInfo->m_strDiskName[0], strDiskName, ngx_strlen(strDiskName));
    *last = '\0';
    last = ngx_cpymem((u_char *)&diskInfo->m_strDiskPath[0], strDiskPath, ngx_strlen(strDiskPath));
    *last = '\0';

    ngx_media_sys_stat_read_mountfile();
    diskInfo->m_nMountFlag = DISK_LOCAL_PATH;
    if(NGX_OK == ngx_media_sys_stat_check_diskmount_state(strDiskPath))
    {
        diskInfo->m_nMountFlag = DISK_MOUNT;
    }

    g_VideoSysStat->DiskInfoList[g_VideoSysStat->DiskCount] = diskInfo;
    g_VideoSysStat->DiskCount++;
    ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,
                      "Add a disk:[%s] to stat moudle OK.",strDiskPath);

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }

    return NGX_OK;
}


ngx_int_t
ngx_media_sys_stat_remove_disk(u_char* strDiskPath)
{
    ngx_uint_t                 i = 0;
    DiskInfo           *diskInfo = NULL;
    ngx_int_t          bFindFlag = 0;

    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }

    if(NULL == strDiskPath)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "Remove a disk from stat moudle failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i  < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];
        if(0 == ngx_strncmp(diskInfo->m_strDiskPath, strDiskPath, ngx_strlen(strDiskPath)))
        {
            bFindFlag = 1;
            break;
        }
    }

    if(!bFindFlag) {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0, "Remove a disk from stat moudle failed.");
        if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
            return NGX_ERROR;
        }
        return NGX_ERROR;
    }

    for(; i < g_VideoSysStat->DiskCount; i++)
    {
        if((i+1) == g_VideoSysStat->DiskCount) {
            g_VideoSysStat->DiskInfoList[i] = NULL;
            g_VideoSysStat->DiskCount--;
            break;
        }
        g_VideoSysStat->DiskInfoList[i]
                          = g_VideoSysStat->DiskInfoList[i+1];
    }

    (void)ngx_pfree(g_VideoSysStat->pool,diskInfo);
    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }

    return NGX_OK;
}

ngx_uint_t 
ngx_media_sys_stat_get_all_disk(u_char** diskNameArray,u_char** diskPathArray,ngx_uint_t max)
{
    ngx_uint_t          i = 0;
    ngx_uint_t          count = 0;
    DiskInfo           *diskInfo = NULL;

    if(NULL == g_VideoSysStat)
    {
        return 0;
    }

    if(NULL == diskNameArray || NULL == diskPathArray)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                      "get all disk from stat moudle failed as the paramter is invalid.");
        return 0;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return 0;
    }

    for(i = 0; (i  < g_VideoSysStat->DiskCount)&&(i <= max) ; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];
        diskNameArray[i] = (u_char*)&diskInfo->m_strDiskName[0];
        diskPathArray[i] = (u_char*)&diskInfo->m_strDiskPath[0];
        count++;
    }

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return 0;
    }

    return count;
}

void
ngx_media_sys_stat_get_cpuinfo(ngx_uint_t *ulUsedPer)
{
    if(NULL == g_VideoSysStat)
    {
        *ulUsedPer = 0;
        return;
    }
    *ulUsedPer = g_VideoSysStat->sysInfo.m_ulCpuUsed[NGX_STAT_INTERVAL_NUM];
    return;
}


 void
ngx_media_sys_stat_get_memoryinfo(ngx_uint_t* ulTotalSize, ngx_uint_t* ulUsedSize)
{
    if(NULL == g_VideoSysStat)
    {
        *ulTotalSize = 1;
        *ulUsedSize  = 0;
        return;
    }
    *ulTotalSize = g_VideoSysStat->sysInfo.m_ulMemTotal;
    *ulUsedSize  = g_VideoSysStat->sysInfo.m_ulMemUsed[NGX_STAT_INTERVAL_NUM];
    return;
}


ngx_int_t
ngx_media_sys_stat_get_networkcardinfo(u_char* strIP, ngx_uint_t* ulTotalSize,
                       ngx_uint_t* ulUsedRecvSize, ngx_uint_t* ulUsedSendSize)
{
    ngx_uint_t                 i = 0;
    NetworkCardInfo* netcardInfo = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }

    if(NULL == strIP)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Get netword card info from stat moudle failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    *ulTotalSize = 0;
    *ulUsedRecvSize = 0;
    *ulUsedSendSize = 0;

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->NetWorkCount; i++)
    {
        netcardInfo = g_VideoSysStat->NetWorkInfoList[i];
        if(0 == ngx_strncmp((const char*)&netcardInfo->m_strIP[0], strIP, ngx_strlen(strIP)))
        {
            *ulTotalSize    = netcardInfo->m_ulBWTotal;
            *ulUsedRecvSize = netcardInfo->m_ulBWUsedRecv[NGX_STAT_INTERVAL_NUM];
            *ulUsedSendSize = netcardInfo->m_ulBWUsedSend[NGX_STAT_INTERVAL_NUM];
            if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
                return NGX_OK;
            }
            return NGX_OK;
        }
    }

    ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Get netword card info:[%s] from stat moudle failed as can't find the IP mark.",
                    strIP);
    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_media_sys_stat_get_diskinfo(u_char* strDiskPath, uint64_t* ullTotalSize, uint64_t* ullUsedSize)
{
    ngx_uint_t                 i = 0;
    DiskInfo           *diskInfo = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NGX_ERROR;
    }

    if(NULL == strDiskPath)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Get disk info from stat moudle failed as the paramter is invalid.");
        return NGX_ERROR;
    }

    *ullTotalSize = 0;
    *ullUsedSize  = 0;

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];
        if(0 == ngx_strncmp((const char*)&diskInfo->m_strDiskPath[0], strDiskPath, ngx_strlen(strDiskPath)))
        {
            *ullTotalSize = (diskInfo->m_ullTotalSize/DISK_UNIT_MB);
            *ullUsedSize = (diskInfo->m_ullUsedSize/DISK_UNIT_MB);

            if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
                return NGX_OK;
            }
            return NGX_OK;
        }
    }

    ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Get disk:[%s] info from stat moudle failed as can't find the disk mark.",
                    strDiskPath);
    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_media_sys_stat_stat_cpuinfo()
{
    ngx_uint_t                      i = 0;
    FILE                        *pf;
    static long long int lastUserTime = 0;
    static long long int lastNiceTime = 0;
    static long long int lastSysTime  = 0;
    static long long int lastIdleTime = 0;
    long long int            userTime = 0;
    long long int            niceTime = 0;
    long long int            systTime = 0;
    long long int            idleTime = 0;
    long long int       totalIdleTime = 0;
    long long int       totalUsedTime = 0;
    u_char strStatInfo[MAX_NAME_LEN + 1] = {'\0', };



    pf = fopen("/proc/stat", "r");

    if (NULL == pf)
    {
        return NGX_ERROR;
    }

    if (0 == fread(strStatInfo, sizeof(u_char), MAX_NAME_LEN, pf))
    {
        fclose(pf);
        return NGX_ERROR;
    }
    fclose(pf);
    u_char *pszTmp = (u_char *)ngx_strstr((const char*)&strStatInfo[0], "cpu ");
    if (NULL == pszTmp)
    {
        return NGX_ERROR;
    }

    sscanf((const char*)&pszTmp[0], "cpu %Ld %Ld %Ld %Ld\n", &userTime, &niceTime, &systTime, &idleTime);//lint !e561 !e566

    if (0 == lastUserTime)
    {
        lastUserTime = userTime;
        lastNiceTime = niceTime;
        lastSysTime = systTime;
        lastIdleTime = idleTime;
        return NGX_OK;
    }

    totalIdleTime = (uint64_t) (idleTime - lastIdleTime);
    totalUsedTime = (uint64_t) ((userTime - lastUserTime)
                             + (niceTime - lastNiceTime)
                             + (systTime - lastSysTime)
                             + (idleTime - lastIdleTime));

    if (1000 > totalUsedTime)
    {
        return NGX_OK;
    }

    lastUserTime = userTime;
    lastNiceTime = niceTime;
    lastSysTime = systTime;
    lastIdleTime = idleTime;

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }
    g_VideoSysStat->sysInfo.m_ulCpuUsed[g_VideoSysStat->ulStatIndex]
                       = (ngx_uint_t)(((totalUsedTime - totalIdleTime)* 100) / totalUsedTime);

    ngx_uint_t * pCpuAverge  = &g_VideoSysStat->sysInfo.m_ulCpuUsed[NGX_STAT_INTERVAL_NUM];
    *pCpuAverge = 0;
    for(i = 0; i < NGX_STAT_INTERVAL_NUM; i++)
    {
        *pCpuAverge += g_VideoSysStat->sysInfo.m_ulCpuUsed[i];
    }

    *pCpuAverge = *pCpuAverge / NGX_STAT_INTERVAL_NUM;

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_media_sys_stat_stat_memoryinfo()
{
    FILE    *fp = NULL;
    char    cBuff[MAX_NAME_LEN+1] = {0, };
    int32_t imemTotal = 0;
    int32_t imemfree = 0;
    int32_t ibuffer = 0;
    int32_t icache = 0;
    ngx_uint_t i = 0;

    fp = fopen("/proc/meminfo", "r");
    if (NULL == fp)
    {
        return NGX_ERROR;
    }
    while (fgets(cBuff, sizeof(cBuff), fp))
    {
        if (!strncmp(cBuff, "MemTotal:", strlen("MemTotal:")))
        {
            sscanf(cBuff, "MemTotal: %d kB", &imemTotal);
        }
        g_VideoSysStat->sysInfo.m_ulMemTotal = (ngx_uint_t)imemTotal;

        if (!strncmp(cBuff, "MemFree:", strlen("MemFree:")))
        {
            sscanf(cBuff, "MemFree: %d kB", &imemfree);
        }

        if (!strncmp(cBuff, "Buffers:", strlen("Buffers:")))
        {
            sscanf(cBuff, "Buffers: %d kB", &ibuffer);
        }

        if (!strncmp(cBuff, "Cached:", strlen("Cached:")))
        {
            sscanf(cBuff, "Cached: %d kB", &icache);
            break;
        }
    }

    (void)fclose(fp);

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    g_VideoSysStat->sysInfo.m_ulMemUsed[g_VideoSysStat->ulStatIndex] =
        g_VideoSysStat->sysInfo.m_ulMemTotal - (ngx_uint_t)(imemfree + ibuffer + icache);

    ngx_uint_t* ulMemAvg = &g_VideoSysStat->sysInfo.m_ulMemUsed[NGX_STAT_INTERVAL_NUM];
    *ulMemAvg = 0;
    for(i = 0; i < NGX_STAT_INTERVAL_NUM; i++)
    {
        *ulMemAvg += g_VideoSysStat->sysInfo.m_ulMemUsed[i];
    }

    *ulMemAvg = *ulMemAvg / NGX_STAT_INTERVAL_NUM;

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_media_sys_stat_stat_diskinfo()
{
    ngx_uint_t                 i = 0;
    DiskInfo           *diskInfo = NULL;

    ngx_media_sys_stat_read_mountfile();
    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];

        if((DISK_MOUNT == diskInfo->m_nMountFlag)
            && (NGX_OK != ngx_media_sys_stat_check_diskmount_state((u_char*)&diskInfo->m_strDiskPath[0])))
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "The Disk's mount flag is offline.diskPath[%s].",diskInfo->m_strDiskPath);

            diskInfo->m_ullTotalSize = 0;
            diskInfo->m_ullUsedSize  = 0;
            continue;
        }

        struct statvfs hdstat;
        if (-1 == statvfs(diskInfo->m_strDiskPath, &hdstat))
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Fail to read disk information.disk[%s].",diskInfo->m_strDiskPath);

            diskInfo->m_ullTotalSize = 0;
            diskInfo->m_ullUsedSize  = 0;
            continue;
        }

        if(ST_RDONLY == hdstat.f_flag)
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                    "Disk's filesystem is realonly.disk[%s].",diskInfo->m_strDiskPath);

            diskInfo->m_ullTotalSize = 0;
            diskInfo->m_ullUsedSize  = 0;
            continue;
        }

        diskInfo->m_ullTotalSize = (uint64_t)hdstat.f_bsize * hdstat.f_blocks;
        uint64_t ullFreeSize = (uint64_t)hdstat.f_bsize * hdstat.f_bavail;
        diskInfo->m_ullUsedSize = diskInfo->m_ullTotalSize - ullFreeSize;
    }

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_media_sys_stat_stat_networkcard_info()
{
    ngx_uint_t                 i = 0;
    NetworkCardInfo* netcardInfo = NULL;

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NGX_ERROR;
    }


    for(i = 0; i < g_VideoSysStat->NetWorkCount; i++)
    {
        netcardInfo = g_VideoSysStat->NetWorkInfoList[i];
        ngx_media_sys_stat_stat_netcard_info(netcardInfo);

        ngx_uint_t* pBWAvgRecv = &netcardInfo->m_ulBWUsedRecv[NGX_STAT_INTERVAL_NUM];
        ngx_uint_t* pBWAvgSend = &netcardInfo->m_ulBWUsedSend[NGX_STAT_INTERVAL_NUM];

        if(0 == netcardInfo->m_ulBWTotal)
        {
            *pBWAvgRecv = 0;
            *pBWAvgSend = 0;

            continue;
        }

        *pBWAvgRecv = 0;
        *pBWAvgSend = 0;
        for( i = 0; i < NGX_STAT_INTERVAL_NUM; i++)
        {
            *pBWAvgRecv += netcardInfo->m_ulBWUsedRecv[i];
            *pBWAvgSend += netcardInfo->m_ulBWUsedSend[i];
        }

        *pBWAvgRecv = *pBWAvgRecv / NGX_STAT_INTERVAL_NUM;
        *pBWAvgSend = *pBWAvgSend / NGX_STAT_INTERVAL_NUM;

        ngx_log_error(NGX_LOG_DEBUG, g_VideoSysStat->log, 0,
                    "Netcard stat info:NetCard[%s:%s], totalBW[%uD]Mbps,Recv[%uD]kbps,Send[%uD]kbps.",
                    netcardInfo->m_strName,
                    netcardInfo->m_strIP,
                    netcardInfo->m_ulBWTotal,
                    netcardInfo->m_ulBWUsedRecv[NGX_STAT_INTERVAL_NUM],
                    netcardInfo->m_ulBWUsedSend[NGX_STAT_INTERVAL_NUM]);
    }

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NGX_OK;
    }
    return NGX_OK;
}

static void
ngx_media_sys_stat_read_mountfile()
{
    g_VideoSysStat->bIsMountsOk = 0;
    FILE                        *pf;
    pf = fopen("/proc/mounts", "r");
    size_t buffSize = sizeof(g_VideoSysStat->strMountsInfo);

    if (NULL == pf)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0, "Open /proc/mounts failed.");
        return;
    }

    memset(g_VideoSysStat->strMountsInfo, 0, buffSize);
    if (0 == fread(g_VideoSysStat->strMountsInfo, sizeof(u_char), buffSize, pf))
    {
        (void)fclose(pf);
        return;
    }

    g_VideoSysStat->bIsMountsOk = 1;
    (void)fclose(pf);

    return;
}

static ngx_int_t
ngx_media_sys_stat_check_diskmount_state(u_char* strDiskPath)
{
    if(NULL == strDiskPath)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0, "Check mount online status failed.The parameter is invalid.");
        return NGX_ERROR;
    }

    if(!g_VideoSysStat->bIsMountsOk)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,  "Check mount online status failed.Read /proc/mounts failed.");
        return NGX_ERROR;
    }

    u_char tempDiskPath[STAT_PATH_LEN + 1];
    u_char* last = ngx_snprintf(tempDiskPath, sizeof(tempDiskPath), " %s ", strDiskPath);
    *last = '\0';

    if (NULL == ngx_strstr(g_VideoSysStat->strMountsInfo, tempDiskPath))
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static void
ngx_media_sys_stat_stat_netcard_info(NetworkCardInfo* pNetworkCard)
{
    in_addr_t serviceIp = inet_addr((char*)&pNetworkCard->m_strIP[0]);

    static int32_t        isock = -1;
    struct ifconf         stIfc;
    struct ifreq         *pIfr = NULL;
    struct ifreq          stArrayIfr[MAX_NETCARD_NUM];
    struct sockaddr_in   *pAddr = NULL;
    ngx_uint_t            i = 0;
    ethtool_cmd_t stEcmd;

    if (-1 == isock)
    {
        isock = socket(AF_INET, SOCK_DGRAM, 0);
        if(0 > isock)
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                "Get netcard info failed. Create socket failed,IP[%s], Name[%s].",
                pNetworkCard->m_strIP,
                pNetworkCard->m_strName);
            return;
        }
    }

    memset(&stIfc, 0, sizeof(stIfc));
    memset(&stEcmd, 0, sizeof(ethtool_cmd_t));
    memset(stArrayIfr, 0, sizeof(stArrayIfr));

    stEcmd.cmd = ETHTOOL_GSET;
    stIfc.ifc_len = sizeof(struct ifreq) * MAX_NETCARD_NUM;
    stIfc.ifc_buf = (char *) stArrayIfr;

    if (ioctl(isock, SIOCGIFCONF, &stIfc) < 0)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                "Get all active netcard failed.ioctl <SIOCGIFCONF> failed,IP[%s], Name[%s].",
                pNetworkCard->m_strIP,
                pNetworkCard->m_strName);
        return;
    }

    pIfr = stIfc.ifc_req;

    int32_t nRet = NGX_ERROR;
    for (i = 0; i < (ngx_uint_t) stIfc.ifc_len; i += sizeof(struct ifreq))
    {
        if (NGX_OK != ioctl(isock, SIOCGIFADDR, pIfr))
        {
            continue;
        }

        pAddr = (struct sockaddr_in *) (void*)&pIfr->ifr_addr;

        if (0 == strncmp(pIfr->ifr_name, "lo", ngx_strlen("lo")))
        {
            pIfr++;
            continue;
        }

        if (serviceIp != pAddr->sin_addr.s_addr)
        {
            pIfr++;
            continue;
        }

        memset(pNetworkCard->m_strName, 0, sizeof(pNetworkCard->m_strName));
        strncpy((char*)&pNetworkCard->m_strName[0], pIfr->ifr_name, strlen(pIfr->ifr_name));

        if (NGX_OK != ioctl(isock, SIOCGIFFLAGS, pIfr))
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                "get netcard%u 's information failed,IP[%s], Name[%s].",
                i,
                pNetworkCard->m_strIP,
                pNetworkCard->m_strName);

                nRet = NGX_ERROR;
                break;
        }

        if (!(pIfr->ifr_flags & IFF_UP) || !(pIfr->ifr_flags & IFF_RUNNING))
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
                "netcard is down or not running, please check it."
                "IP[%s], Name[%s], network name[%s].",
                pNetworkCard->m_strIP,
                pNetworkCard->m_strName,
                pIfr->ifr_name);
            pIfr++;
            continue;
        }
#if 0
        if (0 == strncmp(pIfr->ifr_name, "bond", ngx_strlen("bond")))
        {
            nRet = ngx_media_sys_stat_stat_bondnetcardinfo(isock, pNetworkCard,pIfr);
            break;
        }
        else
        {
            nRet = ngx_media_sys_stat_stat_ethnetcardinfo(isock, pNetworkCard,pIfr);
            break;
        }
#else
        nRet = ngx_media_sys_stat_stat_bondnetcardinfo(isock, pNetworkCard,pIfr);
        if (NGX_OK != nRet)
        {
            nRet = ngx_media_sys_stat_stat_ethnetcardinfo(isock, pNetworkCard,pIfr);
            break;
        }
        break;
#endif
    }

    if(NGX_OK != nRet)
    {
        ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,"Get the network card information failed.IP[%s]",pNetworkCard->m_strIP);
        pNetworkCard->m_ulBWTotal = 0;
        memset(pNetworkCard->m_ulBWUsedRecv, 0, sizeof(pNetworkCard->m_ulBWUsedRecv));
        memset(pNetworkCard->m_ulBWUsedSend, 0, sizeof(pNetworkCard->m_ulBWUsedSend));
        pNetworkCard->m_ullCurrTxByteRecv = 0;
        pNetworkCard->m_ullCurrTxByteSend = 0;
        return;
    }

    ngx_media_sys_stat_stat_BWinfo(pNetworkCard);
    return ;
}

static ngx_int_t
ngx_media_sys_stat_stat_bondnetcardinfo(int32_t isock, NetworkCardInfo* pNetworkCard, struct ifreq* pIfr )
{
    ngx_uint_t ulTotalBW = 0;
    ifbond_t stBondStat;
    u_char                 *last = NULL;
    memset(&stBondStat, 0, sizeof(stBondStat));
    pIfr->ifr_data = (char *) &stBondStat;

    if (NGX_OK != ioctl(isock, SIOCBONDINFOQUERY, pIfr))
    {
        return NGX_ERROR;
    }

    ngx_uint_t i = 0;
    ifslave_t slave_info;
    BondInfo stBondInfo;
    memset(&slave_info, 0, sizeof(slave_info));
    memset(&stBondInfo, 0, sizeof(stBondInfo));

    pIfr->ifr_data = (char *) &slave_info;
    for (i= 0; i < (ngx_uint_t)stBondStat.num_slaves; i++)
    {
        slave_info.slave_id = i;
        if (NGX_OK != ioctl(isock, SIOCBONDSLAVEINFOQUERY, pIfr))
        {
            ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,"ioctl <SIOCBONDSLAVEINFOQUERY> on %s failed.", pIfr->ifr_name);
            continue;
        }

        if (BOND_MODE_ACTIVEBACKUP == stBondStat.bond_mode)
        {
            if(BOND_STATE_ACTIVE == slave_info.state)
            {
                last = ngx_cpymem(stBondInfo.slaveName[0], slave_info.slave_name, ngx_strlen(slave_info.slave_name));
                *last = '\0';
                stBondInfo.slaveNum = 1;
                break;
            }
        }
        else
        {

            last = ngx_cpymem(stBondInfo.slaveName[stBondInfo.slaveNum],
                        slave_info.slave_name,
                        ngx_strlen(slave_info.slave_name));
            *last = '\0';
            ++stBondInfo.slaveNum;
        }
    }

    ethtool_cmd_t       ecmd;
    ecmd.cmd = ETHTOOL_GSET;
    for(i = 0; i < stBondInfo.slaveNum; i++)
    {
        strcpy(pIfr->ifr_name,(char*)&stBondInfo.slaveName[i][0]);
        pIfr->ifr_data = (char *) &ecmd;

        if (NGX_OK != ioctl(isock, SIOCETHTOOL, pIfr))
        {
            ngx_log_error(NGX_LOG_DEBUG, g_VideoSysStat->log, 0,"ioctl <SIOCETHTOOL> on %s failed.", pIfr->ifr_name);
        }
        else if (ABNOMAL_VALUE == ecmd.speed)   /* all the bit is 1 */
        {
            ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,"The network is down.network name[%s].", pIfr->ifr_name);
        }
        else
        {
            ulTotalBW += ecmd.speed;
        }
    }

    if(0 == ulTotalBW)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
            "Get bond bandwidth is failed. IP[%s], Name[%s].",
            pNetworkCard->m_strIP,
            pNetworkCard->m_strName);

        return NGX_ERROR;
    }

    pNetworkCard->m_ulBWTotal = ulTotalBW;

    return NGX_OK;
}


static ngx_int_t
ngx_media_sys_stat_stat_ethnetcardinfo(int32_t isock, NetworkCardInfo* pNetworkCard, struct ifreq* pIfr )
{
    ethtool_cmd_t stEcmd;
    memset(&stEcmd, 0, sizeof(ethtool_cmd_t));
    stEcmd.cmd = ETHTOOL_GSET;

    pIfr->ifr_data = (char *) &stEcmd;
    if (NGX_OK != ioctl(isock, SIOCETHTOOL, pIfr))
    {
        ngx_log_error(NGX_LOG_DEBUG, g_VideoSysStat->log, 0,"ioctl <SIOCETHTOOL> on %s failed.", pIfr->ifr_name);
        pNetworkCard->m_ulBWTotal = NGX_BANDWIDTH_DEFAULT;
        return NGX_OK;
    }
    else if (ABNOMAL_VALUE == stEcmd.speed)
    {
        ngx_log_error(NGX_LOG_INFO, g_VideoSysStat->log, 0,"The network is down.network name[%s]",pIfr->ifr_name);
        pNetworkCard->m_ulBWTotal = NGX_BANDWIDTH_DEFAULT;
        return NGX_OK;
    }

    pNetworkCard->m_ulBWTotal = stEcmd.speed;

    return NGX_OK;
}


static void
ngx_media_sys_stat_stat_BWinfo(NetworkCardInfo* pNetworkCard)
{
    FILE          *fp = NULL;
    fp = fopen("/proc/net/dev", "r");
    if (NULL == fp)
    {
        ngx_log_error(NGX_LOG_WARN, g_VideoSysStat->log, 0,
            "Stat bandwidth failed as open /proc/net/dev failed.IP[%s], name[%s]",
            pNetworkCard->m_strIP,
            pNetworkCard->m_strName);

        fp = NULL;
        return ;
    }

    u_char cline[1024] = {0, };
    u_char    *pszcp1 = NULL;
    u_char    *pszcp2 = NULL;
    long unsigned int ullrecvBytes = 0;
    long unsigned int ullsendBytes = 0;
    long unsigned int ultmp = 0;
    while (fgets((char*)&cline[0], sizeof(cline), fp))
    {
        pszcp1 = cline;

        while (isspace(*pszcp1))
        {
            pszcp1++;
        }

        pszcp2 = (u_char*)ngx_strchr(pszcp1, ':');

        if (NULL == pszcp2)
        {
            continue;
        }
        if((0 != ngx_strncmp(pszcp1, "eth", ngx_strlen("eth")))
        && (0 != ngx_strncmp(pszcp1, "bond", ngx_strlen("bond"))))
        {
            continue;            /* if ':' no found read the next cline or the
                                   header line or 'lo' 'sit' card */
        }

        *pszcp2 = '\0';
        pszcp2++;
        if (0 != ngx_strncmp(pszcp1, pNetworkCard->m_strName,ngx_strlen(pNetworkCard->m_strName)))
        {
            continue;
        }

        sscanf((char*)pszcp2,
               "%lu %lu%lu%lu%lu%lu%lu%lu  %lu    %lu%lu%lu%lu%lu%lu%lu",
               &ullrecvBytes,
               &ultmp, &ultmp, &ultmp, &ultmp, &ultmp, &ultmp, &ultmp,
               &ullsendBytes,
               &ultmp, &ultmp, &ultmp, &ultmp, &ultmp, &ultmp, &ultmp);


        if (0 == pNetworkCard->m_ullCurrTxByteRecv)
        {
            pNetworkCard->m_ullCurrTxByteRecv = ullrecvBytes - 1; //first is 1kb/s
        }

        //change unit to kb/s
        ngx_uint_t* pBWUsed = &pNetworkCard->m_ulBWUsedRecv[g_VideoSysStat->ulStatIndex];
        *pBWUsed = (ngx_uint_t)(((ullrecvBytes - pNetworkCard->m_ullCurrTxByteRecv) * 8)
            / (1024 * NGX_STAT_DEFAULT_INTERVAL));
        if (*pBWUsed > (pNetworkCard->m_ulBWTotal* 1024))
        {
            *pBWUsed = 1;
        }
        pNetworkCard->m_ullCurrTxByteRecv = ullrecvBytes;

        if (0 == pNetworkCard->m_ullCurrTxByteSend)
        {
            pNetworkCard->m_ullCurrTxByteSend = ullsendBytes - 1; //first is 1kb/s
        }

        //change unit to kb/s
        pBWUsed = &pNetworkCard->m_ulBWUsedSend[g_VideoSysStat->ulStatIndex];
        *pBWUsed = (ngx_uint_t)(((ullsendBytes - pNetworkCard->m_ullCurrTxByteSend) * 8)
            / (1024 * NGX_STAT_DEFAULT_INTERVAL));
        if (*pBWUsed > (pNetworkCard->m_ulBWTotal* 1024))
        {
            *pBWUsed = 1;
        }
        pNetworkCard->m_ullCurrTxByteSend = ullsendBytes;

        break;
    }

    (void)fclose(fp);

    return;
}

u_char*   
ngx_media_sys_map_uri_to_path(ngx_http_request_t *r, ngx_str_t *path,size_t *root_length, size_t reserved)
{
    ngx_uint_t                 i = 0;
    size_t                  size = 0; 
    size_t               sufsize = 0;
    size_t                 total = 0; 
    DiskInfo           *diskInfo = NULL;
    u_char             *last     = NULL;
    u_char             *pos      = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NULL;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NULL;
    }

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];

        pos = (u_char *)ngx_strstr(r->uri.data,&diskInfo->m_strDiskName[0]);
        if(NULL == pos) {
            continue;
        }
        if('/' != *pos) {
            continue;
        }
        size   = ngx_strlen(diskInfo->m_strDiskPath);
        sufsize  = (&r->uri.data[r->uri.len]) - pos;
        total  = sufsize + size + 1;
        path->data = ngx_pcalloc(r->pool,total);
        path->len  = total;

        last = ngx_cpymem(path->data,diskInfo->m_strDiskPath,size);
        last = ngx_cpymem(last,pos,sufsize);
        *last = '\0';

        *root_length = sufsize;
        break;
    }
    (void)ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log);

    if(NULL != last) {
        return last;
    }

    return ngx_http_map_uri_to_path(r,path,root_length,reserved);
}

u_char*   
ngx_media_sys_map_vfile_to_path(ngx_http_request_t *r, ngx_str_t *path,ngx_str_t* vfile)
{
    ngx_uint_t                 i = 0;
    size_t                  size = 0; 
    size_t                 total = 0; 
    DiskInfo           *diskInfo = NULL;
    u_char             *last     = NULL;
    u_char             *pos      = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NULL;
    }
    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media sys stat ngx_media_sys_map_vfile_to_path lock fail.");
        return NULL;
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx_media_sys_map_vfile_to_path: disk count[%d]",g_VideoSysStat->DiskCount);

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];

        size       = ngx_strlen(diskInfo->m_strDiskName);

        pos = (u_char *)ngx_strstr(r->uri.data,&diskInfo->m_strDiskName[0]);
        if(NULL == pos) {
            ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx media sys stat map disk,find name:[%s] to uri:[%V] fail.",
                          &diskInfo->m_strDiskName[0],&r->uri);
            continue;
        }
        if('/' != pos[size]) {
            ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                          "ngx media sys stat map disk name:[%s] to uri:[%V],end:[%c] fail.",
                          &diskInfo->m_strDiskName[0],&r->uri,*pos);
            continue;
        }
        size       = ngx_strlen(diskInfo->m_strDiskPath);
        total      = size + vfile->len + 2; /* VPATH / FILE \0 */
        path->data = ngx_pcalloc(r->pool,total);
        path->len  = total;

        last = ngx_cpymem(path->data,diskInfo->m_strDiskPath,size);
        *last = '/';
        last++;
        last = ngx_cpymem(last,vfile->data,vfile->len);
        *last = '\0';
        break;
    }
    (void)ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log);

    return last;
}
u_char*  
ngx_media_sys_map_vpath_vfile_to_path(ngx_http_request_t *r,ngx_str_t* vpath,ngx_str_t* vfile,ngx_str_t *path)
{
    ngx_uint_t                 i = 0;
    ngx_uint_t             total = 0;
    ngx_uint_t          presize  = 0;
    DiskInfo           *diskInfo = NULL;
    u_char                 *last = NULL;

    if(NULL == g_VideoSysStat)
    {
        return NULL;
    }

    if(NULL == vpath || NULL == vfile)
    {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                    "map vpath vfile to full path failed as the paramter is invalid.");
        return NULL;
    }

    if(ngx_thread_mutex_lock(&g_VideoSysStat->StatMutex, g_VideoSysStat->log) != NGX_OK) {
        return NULL;
    }

    for(i = 0; i < g_VideoSysStat->DiskCount; i++)
    {
        diskInfo = g_VideoSysStat->DiskInfoList[i];
        if(0 == ngx_memcmp((const char*)&diskInfo->m_strDiskName[0], vpath->data, vpath->len))
        {
            presize  = ngx_strlen((char*)&diskInfo->m_strDiskPath[0]);
            total    =  presize + vfile->len + 2;
            path->data = ngx_pcalloc(r->pool,total);
            path->len  = total;
            last = ngx_cpymem(path->data,(char*)&diskInfo->m_strDiskPath[0],presize);
            *last = '/';
            last++;
            last = ngx_cpymem(last,vfile->data, vfile->len);
            *last = '\0';
            break;
        }
    }

    if (ngx_thread_mutex_unlock(&g_VideoSysStat->StatMutex,g_VideoSysStat->log) != NGX_OK) {
        return NULL;
    }

    return last;
}