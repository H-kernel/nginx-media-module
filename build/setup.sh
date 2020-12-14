#!/bin/bash
#set -x
set -o nounset
ALLMEDIAVERSION="v1.17.4"
CURRENT_PATH=`pwd`
cd ${CURRENT_PATH}/..
export ALLMEDIA_ROOT=$PWD
export PREFIX_ROOT=/home/allmedia/
export THIRD_ROOT=${CURRENT_PATH}/3rd_party/
export EXTEND_ROOT=${CURRENT_PATH}/extend/
export PATCH_ROOT=${CURRENT_PATH}/patch/
export SCRIPT_ROOT=${CURRENT_PATH}/script/

find=`env|grep PKG_CONFIG_PATH`    
if [ "find${find}" == "find" ]; then    
    export PKG_CONFIG_PATH=${EXTEND_ROOT}/lib/pkgconfig/
else
    export PKG_CONFIG_PATH=${EXTEND_ROOT}/lib/pkgconfig/:${PKG_CONFIG_PATH}
fi

find=`env|grep PATH`
if [ "find${find}" == "find" ]; then    
    export PATH=${EXTEND_ROOT}/bin/
else
    export PATH=${EXTEND_ROOT}/bin/:${PATH}
fi
echo "------------------------------------------------------------------------------"
echo " PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"
echo " PATH ${PATH}"
echo " ALLMEDIA_ROOT exported as ${ALLMEDIA_ROOT}"
echo "------------------------------------------------------------------------------"

host_type=`uname -p`

config_args=""

if [ "$host_type" = "aarch64" ];then
   config_args="--host=arm-linux --build=arm-linux"
fi

#gcc_version=`gcc -dumpversion`
gcc_version=`gcc -E -dM - </dev/null|grep __VERSION__|awk -F'"' '{print $2}'|awk '{print $1}'`

#WITHDEBUG="--with-debug"
export WITHDEBUG=""
#NGX_LINK="--add-dynamic-module"
NGX_LINK="--add-module"
#
# Sets QUIT variable so script will finish.
#
quit()
{
    QUIT=$1
}

download_3rd()
{
    if [ ! -f ${THIRD_ROOT}/3rd.list ]; then
        echo "there is no 3rd package list\n"
        return 1
    fi
    cat ${THIRD_ROOT}/3rd.list|while read LINE
    do
        name=`echo "${LINE}"|awk -F '|' '{print $1}'`
        url=`echo "${LINE}"|awk -F '|' '{print $2}'`
        package=`echo "${LINE}"|awk -F '|' '{print $3}'`
        if [ ! -f ${THIRD_ROOT}/${package} ]; then
            echo "begin:download :${name}..................."
            wget --no-check-certificate ${url} -O ${THIRD_ROOT}/${package}
            echo "end:download :${name}....................."
        fi     
    done
    return 0
}

build_xzutils()
{
    module_pack="xz-5.2.2.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the xzutils package from server\n"
        wget http://tukaani.org/xz/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd xz*/
    ./configure ${config_args} --prefix=${EXTEND_ROOT} --with-pic=yes
                
    if [ 0 -ne ${?} ]; then
        echo "configure xzutils fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build xzutils fail!\n"
        return 1
    fi
    
    return 0
}

build_libxml2()
{
    module_pack="Python-2.7.tgz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the Python package from server\n"
        wget https://www.python.org/ftp/python/2.7/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd Python*/
    PYTHON_ROOT=`pwd`
    
    module_pack="libxml2-2.9.7.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libxml2 package from server\n"
        wget ftp://xmlsoft.org/libxml2/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libxml2*/
    ./configure ${config_args} --prefix=${EXTEND_ROOT} --enable-shared=no --with-sax1 --with-zlib=${EXTEND_ROOT} --with-iconv=${EXTEND_ROOT} --with-python=${PYTHON_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure libxml2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libxml2 fail!\n"
        return 1
    fi
    
    return 0
}

build_pcre()
{
    module_pack="pcre-8.39.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the pcre package from server\n"
        wget https://sourceforge.net/projects/pcre/files/pcre/8.39/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd pcre*/
    ./configure ${config_args} --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure pcre fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build pcre fail!\n"
        return 1
    fi
    
    return 0
}

build_zlib()
{
    module_pack="zlib-1.2.8.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zlib package from server\n"
        wget http://zlib.net/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zlib*/
    ./configure --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure zlib fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zlib fail!\n"
        return 1
    fi
    
    return 0
}

build_libiconv()
{
    module_pack="libiconv-1.16.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libiconv package from server\n"
        wget http://ftp.gnu.org/pub/gnu/libiconv/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libiconv*/
    patch -p0 <${PATCH_ROOT}/libiconv.patch
    ./configure ${config_args} --prefix=${EXTEND_ROOT} --enable-static=yes
                
    if [ 0 -ne ${?} ]; then
        echo "configure libiconv fail!\n"
        return 1
    fi
    
    make clean  
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libiconv fail!\n"
        return 1
    fi
    
    return 0
}

build_bzip2()
{
    module_pack="bzip2-1.0.6.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the bzip2 package from server\n"
        wget http://www.bzip.org/1.0.6/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd bzip2*/
    #./configure ${config_args} --prefix=${EXTEND_ROOT}
    EXTEND_ROOT_SED=$(echo ${EXTEND_ROOT} |sed -e 's/\//\\\//g')
    sed -i "s/PREFIX\=\/usr\/local/PREFIX\=${EXTEND_ROOT_SED}/" Makefile    
    sed -i "s/CFLAGS=-Wall -Winline -O2 -g/CFLAGS\=-Wall -Winline -O2 -fPIC -g/" Makefile
    if [ 0 -ne ${?} ]; then
        echo "sed bzip2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build bzip2 fail!\n"
        return 1
    fi
    
    return 0
}


build_openssl()
{
    module_pack="OpenSSL_1_0_2s.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the openssl package from server\n"
        wget https://www.openssl.org/source/old/0.9.x/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd openssl*/
                    
    if [ 0 -ne ${?} ]; then
        echo "get openssl fail!\n"
        return 1
    fi

    ./config shared --prefix=${EXTEND_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "config openssl fail!\n"
        return 1
    fi
    
    make clean
    
    make
    if [ 0 -ne ${?} ]; then
        echo "make openssl fail!\n"
        return 1
    fi
    make test
    if [ 0 -ne ${?} ]; then
        echo "make test openssl fail!\n"
        return 1
    fi
    make install_sw
    if [ 0 -ne ${?} ]; then
        echo "make install openssl fail!\n"
        return 1
    fi
    
    return 0
}

build_zookeeper()
{
    module_pack="zookeeper-3.4.14.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zookeeper package from server\n"
        wget https://mirrors.tuna.tsinghua.edu.cn/apache/zookeeper/zookeeper-3.4.14/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zookeeper*/
    cd zookeeper-client/zookeeper-client-c
    
    ./configure ${config_args} --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure zookeepr fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zookeepr fail!\n"
        return 1
    fi
    
    return 0
}



build_curl_module()
{
    module_pack="curl-7.66.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the curl package from server\n"
        wget https://github.com/curl/curl/releases/download/curl-7_66_0/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd curl*/
    
    ./configure ${config_args} --prefix=${EXTEND_ROOT} --with-ssl=${EXTEND_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure curl fail!\n"
        return 1
    fi
    make clean            
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build curl fail!\n"
        return 1
    fi
    
    return 
}

build_minixml_module()
{
    module_pack="mxml-2.10.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the minixml package from server\n"
        wget http://www.msweet.org/files/project3/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd mxml*/
    
    ./configure ${config_args} --prefix=${EXTEND_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure minixml fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build minixml fail!\n"
        return 1
    fi
    
    return 0
}


build_zeromq()
{
    module_pack="zeromq-4.2.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zeromq package from server\n"
        wget https://github.com/zeromq/libzmq/releases/download/v4.2.5/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zeromq*/
    ./configure ${config_args} --prefix=${EXTEND_ROOT}
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zeromq fail!\n"
        return 1
    fi
    
    return 0
}

build_extend_modules()
{
    download_3rd
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_bzip2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zlib
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_pcre
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libiconv
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_xzutils
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libxml2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_openssl
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zookeeper
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_minixml_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_curl_module
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zeromq
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    return 0
}

build_mk_module()
{   
    ####download the mediakernel ####
    export mediakernel=libMediakenerl-${host_type}-gcc${gcc_version}.tar.gz
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}/${mediakernel} ]; then
        
        if [ ! -f ${THIRD_ROOT}/${mediakernel} ]; then
            echo "begin:download :${mediakernel}..................."
            wget --no-check-certificate http://139.9.183.199:10608/release/${mediakernel} -O ${THIRD_ROOT}/${mediakernel}
            echo "end:download :${mediakernel}....................."
        fi
    fi

    tar -zxvf ${THIRD_ROOT}/${mediakernel} -C ${ALLMEDIA_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "unpack the libMediakenerl fail."
        return 1
    fi 

    cd ${ALLMEDIA_ROOT}/libMediakenerl/
    if [ 0 -ne ${?} ]; then
        echo "the libMediakenerl path is not exsit."
        return 1
    fi 

    return 0
}

build_allmedia_module()
{
    cd ${ALLMEDIA_ROOT}/nginx/
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the allmedia fail!\n"
       return 1
    fi
    mkdir -p ${PREFIX_ROOT}/lib/
    cp -R ${ALLMEDIA_ROOT}/libMediakenerl/lib/libMediaKenerl.so* ${PREFIX_ROOT}/lib/
    cp -R ${ALLMEDIA_ROOT}/libMediakenerl/lib/libasrtspsvr.so* ${PREFIX_ROOT}/lib/
    echo "make the allmedia success!\n"
    return 0
}

rebuild_allmedia_module()
{

    ###wget the allmedia    
    
    basic_opt=" --prefix=${PREFIX_ROOT} ${WITHDEBUG} 
                --sbin-path=sbin/allmedia
                --with-threads 
                --with-file-aio 
                --with-http_ssl_module 
                --with-http_realip_module 
                --with-http_addition_module 
                --with-http_sub_module 
                --with-http_dav_module 
                --with-http_flv_module 
                --with-http_mp4_module 
                --with-http_gunzip_module 
                --with-http_gzip_static_module 
                --with-http_random_index_module 
                --with-http_secure_link_module 
                --with-http_stub_status_module 
                --with-http_auth_request_module 
                --with-mail 
                --with-mail_ssl_module 
                --with-cc-opt=-O3 "
                
    
    third_opt=""
    cd ${THIRD_ROOT}/pcre*/
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-pcre=`pwd`"
    fi
    cd ${THIRD_ROOT}/zlib*/
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt}
                    --with-zlib=`pwd`"
    fi
    cd ${THIRD_ROOT}/openssl*/
    if [ 0 -eq ${?} ]; then
        third_opt="${third_opt} 
                    --with-openssl=`pwd`"
    fi
    
    module_opt="" 
    cd ${ALLMEDIA_ROOT}
    if [ 0 -eq ${?} ]; then
        module_opt="${module_opt} 
                     --add-module=`pwd`"
        LD_LIBRARY_PATH=${EXTEND_ROOT}/lib
        LIBRARY_PATH=${EXTEND_ROOT}/lib
        C_INCLUDE_PATH=${EXTEND_ROOT}/include
        export LD_LIBRARY_PATH LIBRARY_PATH C_INCLUDE_PATH
        dos2unix config
    fi
        
    all_opt="${basic_opt} ${third_opt} ${module_opt}"
    
    echo "all optiont info:\n ${all_opt}"
    
    cd ${ALLMEDIA_ROOT}/nginx/
    chmod +x configure
    ./configure ${all_opt} 

    if [ 0 -ne ${?} ]; then
       echo "configure the allmedia fail!\n"
       return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
       echo "make the allmedia fail!\n"
       return 1
    fi
    cp ${SCRIPT_ROOT}/start ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/stop ${PREFIX_ROOT}/sbin
    chmod +x ${PREFIX_ROOT}/sbin/start
    chmod +x ${PREFIX_ROOT}/sbin/stop
    dos2unix ${PREFIX_ROOT}/sbin/start
    dos2unix ${PREFIX_ROOT}/sbin/stop
    cp ${SCRIPT_ROOT}/*.conf ${PREFIX_ROOT}/conf
    mkdir -p ${PREFIX_ROOT}/music/
    cp ${CURRENT_PATH}/music/* ${PREFIX_ROOT}/music/
    cp -R ${CURRENT_PATH}/templet ${PREFIX_ROOT}/conf/
    mkdir -p ${PREFIX_ROOT}/wrk/
    mkdir -p ${PREFIX_ROOT}/logs/
    mkdir -p ${PREFIX_ROOT}/lib/
    cp -R ${ALLMEDIA_ROOT}/libMediakenerl/lib/libMediaKenerl.so* ${PREFIX_ROOT}/lib/
    cp -R ${ALLMEDIA_ROOT}/libMediakenerl/lib/libasrtspsvr.so* ${PREFIX_ROOT}/lib/
    

    echo "make the allmedia success!\n"
    cd ${ALLMEDIA_ROOT}
    return 0
}

rebuild_allmedia_module_debug()
{
    export WITHDEBUG="--with-debug"
    rebuild_allmedia_module
}

clean_all()
{
    cd ${PREFIX_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    
    cd ${THIRD_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    cd ${EXTEND_ROOT}/
    if [ 0 -eq ${?} ]; then
       rm -rf ./*
    fi
    echo "clean all success!\n"
}

package_all()
{
    cd ${CURRENT_PATH}
    #download the music resource
    if [ ! -f ${CURRENT_PATH}/music.tar.gz ]; then
        echo "begin:download :music.tar.gz..................."
        wget --no-check-certificate http://139.9.183.199:10608/release/music.tar.gz
        echo "end:download :music.tar.gz....................."
    fi

    tar -zxvf ${CURRENT_PATH}/music.tar.gz
    if [ 0 -ne ${?} ]; then
        echo "unpack the music fail."
        return
    fi 

    cur_time=`date +%Y%m%d%H%M%S`    
    cd ${PREFIX_ROOT}
    cd sbin/
    ./stop
    cd ${PREFIX_ROOT} 
    rm -rf logs/*
    rm -rf *_temp
    rm -rf sbin/allmedia.old
    cd ../

    


    cp ${SCRIPT_ROOT}/start ${PREFIX_ROOT}/sbin
    cp ${SCRIPT_ROOT}/stop ${PREFIX_ROOT}/sbin
    chmod +x ${PREFIX_ROOT}/sbin/start
    chmod +x ${PREFIX_ROOT}/sbin/stop
    dos2unix ${PREFIX_ROOT}/sbin/start
    dos2unix ${PREFIX_ROOT}/sbin/stop
    cp ${SCRIPT_ROOT}/*.conf ${PREFIX_ROOT}/conf
    mkdir -p ${PREFIX_ROOT}/music/
    cp ${CURRENT_PATH}/music/* ${PREFIX_ROOT}/music/
    cp -R ${CURRENT_PATH}/templet ${PREFIX_ROOT}/conf/
    mkdir -p ${PREFIX_ROOT}/wrk/
    mkdir -p ${PREFIX_ROOT}/logs/
    mkdir -p ${PREFIX_ROOT}/var/
    mkdir -p ${PREFIX_ROOT}/var/sock/
    
    tar -zcvf allmedia-${host_type}-${ALLMEDIAVERSION}-gcc${gcc_version}-${cur_time}.tar.gz allmedia/
    echo "package all success!\n"
}



build_all_media()
{
        
    build_extend_modules
    if [ 0 -ne ${?} ]; then
        return
    fi 
    build_mk_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    rebuild_allmedia_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    echo "make the all modules success!\n"
    cd ${ALLMEDIA_ROOT}
}

build_all_media_debug()
{
    export WITHDEBUG="--with-debug"
    build_all_media
}

build_all_media_release()
{
    export WITHDEBUG=""
    build_all_media
}

all_allmedia_func()
{
        TITLE="Setup the allmedia module"

        TEXT[1]="rebuild the allmedia module"
        FUNC[1]="rebuild_allmedia_module"
        
        TEXT[2]="rebuild the allmedia module(debug)"
        FUNC[2]="rebuild_allmedia_module_debug"
        
        TEXT[3]="build the allmedia module"
        FUNC[3]="build_allmedia_module"
}

all_modules_func()
{
        TITLE="Setup all the 3rd module"

        TEXT[1]="build the 3rd module"
        FUNC[1]="build_extend_modules"
        
        TEXT[2]="build the media kenerl module"
        FUNC[2]="build_mk_module"
        
}

all_func()
{
        TITLE="build module  "
        
        TEXT[1]="build allmedia module"
        FUNC[1]="build_all_media_release"
        
        TEXT[2]="build allmedia module(debug)"
        FUNC[2]="build_all_media_debug"
            
        TEXT[3]="package module"
        FUNC[3]="package_all"
        
        TEXT[4]="clean module"
        FUNC[4]="clean_all"
}
STEPS[1]="all_func"
STEPS[2]="all_modules_func"
STEPS[3]="all_allmedia_func"

QUIT=0

while [ "$QUIT" == "0" ]; do
    OPTION_NUM=1
    if [ ! -x "`which wget 2>/dev/null`" ]; then
        echo "Need to install wget."
        break 
    fi
    for s in $(seq ${#STEPS[@]}) ; do
        ${STEPS[s]}

        echo "----------------------------------------------------------"
        echo " Step $s: ${TITLE}"
        echo "----------------------------------------------------------"

        for i in $(seq ${#TEXT[@]}) ; do
            echo "[$OPTION_NUM] ${TEXT[i]}"
            OPTIONS[$OPTION_NUM]=${FUNC[i]}
            let "OPTION_NUM+=1"
        done

        # Clear TEXT and FUNC arrays before next step
        unset TEXT
        unset FUNC

        echo ""
    done

    echo "[$OPTION_NUM] Exit Script"
    OPTIONS[$OPTION_NUM]="quit"
    echo ""
    echo -n "Option: "
    read our_entry
    echo ""
    ${OPTIONS[our_entry]} ${our_entry}
    echo
done
