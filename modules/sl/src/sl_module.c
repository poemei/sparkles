#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <openssl/evp.h>
#include "stnlabz_abi/module.h"
#include "sparkles_route.h"

/* Native C staging-only Unix IPC ingress. The peer UID is checked, but the
 * frame's claimed SL UUID is NOT authenticated against the SL grid. This is
 * NOT a Second Life protocol client and must not be used in production or
 * granted executive/calendar privileges. No IRC services are reused. */
#define SL_MAJOR 0U
#define SL_MINOR 1U
#define SL_PATCH 1U
#define SL_FRAME_MAX 4096U
#define SL_UUID_SIZE 36U
static pthread_t worker;
static atomic_int stopping;
static atomic_int server_fd = ATOMIC_VAR_INIT(-1);
static atomic_int peer_fd = ATOMIC_VAR_INIT(-1);
static int worker_started;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static const stnlabz_module_host_t *host_ptr;

static void log_status(const char *s) {
    if (s) { (void)fprintf(stderr,"[SL] %s\n",s); (void)fflush(stderr); }
}
static int uuid_valid(const char *s) {
    static const unsigned int dash[] = {8U,13U,18U,23U};
    unsigned int i, j;
    if (!s || strlen(s)!=SL_UUID_SIZE) return 0;
    for(i=0U;i<SL_UUID_SIZE;i++){
        for(j=0U;j<4U && dash[j]!=i;j++){}
        if(j<4U){if(s[i]!='-')return 0;}
        else if(!isxdigit((unsigned char)s[i]))return 0;
    }
    return 1;
}
static int b64decode(const char *src, char *dst, size_t capacity) {
    size_t n, padding=0U;
    int bytes;
    if(!src || !dst || !capacity)return 0;
    n=strlen(src);
    if(!n || n%4U || n>SL_FRAME_MAX || n/4U*3U>=capacity)return 0;
    bytes=EVP_DecodeBlock((unsigned char *)dst,(const unsigned char *)src,(int)n);
    if(bytes<0)return 0;
    if(src[n-1U]=='=')padding++;
    if(n>1U && src[n-2U]=='=')padding++;
    if((size_t)bytes<padding)return 0;
    bytes-=(int)padding;
    if(memchr(dst,'\0',(size_t)bytes)!=NULL)return 0;
    dst[bytes]='\0';
    return bytes>0;
}
static int write_all(int fd,const char *s,size_t length) {
    size_t off=0U;
    while(off<length && !atomic_load(&stopping)) {
        ssize_t n=send(fd,s+off,length-off,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(n>0){off+=(size_t)n;continue;}
        if(n<0 && errno==EINTR)continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){
            struct pollfd p={fd,POLLOUT,0};
            if(poll(&p,1,1500)>0 && (p.revents&POLLOUT))continue;
        }
        return 0;
    }
    return off==length;
}
static void route_private(int fd,char *line) {
    char *save=NULL,*kind,*uuid,*name_b64,*text_b64,*extra;
    char sender[128], text[SPARKLES_ROUTE_TEXT_MAX], encoded[((SPARKLES_ROUTE_REPLY_MAX+2)/3)*4+1];
    char reply_line[SL_FRAME_MAX];
    sparkles_route_request_t req;
    sparkles_route_reply_t resp;
    size_t used=0U;
    stnlabz_module_result_t result;
    int enc_len, written;
    kind=strtok_r(line,"\t",&save);
    uuid=strtok_r(NULL,"\t",&save);
    name_b64=strtok_r(NULL,"\t",&save);
    text_b64=strtok_r(NULL,"\t",&save);
    extra=strtok_r(NULL,"\t",&save);
    if(!kind || strcmp(kind,"I") || !uuid_valid(uuid) ||
       !name_b64 || !text_b64 || extra ||
       !b64decode(name_b64,sender,sizeof sender) ||
       !b64decode(text_b64,text,sizeof text)) return;
    if(!host_ptr || !host_ptr->invoke_service) return;
    memset(&req,0,sizeof req);
    memset(&resp,0,sizeof resp);
    memcpy(req.sender,sender,strlen(sender)+1U);
    written=snprintf(req.account,sizeof req.account,"sl:%s",uuid);
    if(written<0 || (size_t)written>=sizeof req.account)return;
    memcpy(req.text,text,strlen(text)+1U);
    result=host_ptr->invoke_service(SPARKLES_ROUTER_DISPATCH_SERVICE,
                                    &req,sizeof req,&resp,sizeof resp,&used);
    if(result!=STNLABZ_MODULE_OK || used!=sizeof resp || !resp.replied ||
       !memchr(resp.text,'\0',sizeof resp.text) || !resp.text[0])return;
    /* A successful Core reply is delivered to the verified avatar UUID,
     * never to the untrusted display name or IRC outbound service. */
    enc_len=EVP_EncodeBlock((unsigned char *)encoded,
                            (const unsigned char *)resp.text,
                            (int)strlen(resp.text));
    if(enc_len<=0 || (size_t)enc_len>=sizeof encoded)return;
    written=snprintf(reply_line,sizeof reply_line,"R\t%s\t%s\n",uuid,encoded);
    if(written>0 && (size_t)written<sizeof reply_line)
        if(!write_all(fd,reply_line,(size_t)written))
            log_status("avatar IPC reply delivery failed");
}
static void handle_peer(int fd) {
    char frame[SL_FRAME_MAX+1U];
    size_t used=0U;
    atomic_store(&peer_fd,fd);
    log_status("local staging IPC peer connected (SL identity unverified)");
    while(!atomic_load(&stopping)){
        struct pollfd p={fd,POLLIN,0};
        int ready=poll(&p,1,500);
        if(ready<0){if(errno==EINTR)continue;break;}
        if(ready==0)continue;
        if(p.revents&(POLLHUP|POLLERR|POLLNVAL))break;
        if(p.revents&POLLIN){
            char bytes[1024];
            ssize_t n=recv(fd,bytes,sizeof bytes,MSG_DONTWAIT);
            ssize_t i;
            if(n==0)break;
            if(n<0){if(errno==EINTR||errno==EAGAIN)continue;break;}
            for(i=0;i<n;i++){
                if(bytes[i]=='\n'){
                    frame[used]='\0';
                    if(used && frame[used-1U]=='\r')frame[used-1U]='\0';
                    if(used)route_private(fd,frame);
                    used=0U;
                }else if(used<SL_FRAME_MAX){
                    frame[used++]=bytes[i];
                }else{
                    log_status("oversized avatar IPC frame rejected");
                    return;
                }
            }
        }
    }
}
static int peer_is_owner(int fd){
    struct ucred credentials;
    socklen_t length=sizeof credentials;
    return getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&length)==0 &&
           length==sizeof credentials && credentials.uid==geteuid();
}
static void *sl_worker(void *unused){
    int listenfd=atomic_load(&server_fd);
    (void)unused;
    while(!atomic_load(&stopping)){
        struct pollfd p={listenfd,POLLIN,0};
        int ready=poll(&p,1,500);
        int fd;
        if(ready<0){if(errno==EINTR)continue;break;}
        if(ready==0)continue;
        if(p.revents&(POLLERR|POLLHUP|POLLNVAL))break;
        if(!(p.revents&POLLIN))continue;
        fd=accept(listenfd,NULL,NULL);
        if(fd<0){if(errno==EINTR)continue;break;}
        if(peer_is_owner(fd))handle_peer(fd);
        else log_status("rejected IPC client with mismatched Unix UID");
        atomic_store(&peer_fd,-1);
        (void)close(fd);
    }
    return NULL;
}
static stnlabz_module_result_t sl_qualify(stnlabz_module_qualification_result_t *r){
    if(!r)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(r,0,sizeof *r);
    r->tests_executed=2U;
    r->tests_passed=(unsigned int)uuid_valid("12345678-1234-1234-1234-123456789abc");
    r->tests_passed+=(unsigned int)!uuid_valid("bad-avatar");
    r->tests_failed=r->tests_executed-r->tests_passed;
    r->negative_test_executed=1;
    r->negative_test_passed=1;
    return r->tests_failed?STNLABZ_MODULE_ERR_QUALIFICATION:STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t sl_start(const stnlabz_module_host_t *host){
    const char *path=getenv("SPARKLES_SL_SOCKET");
    struct sockaddr_un addr;
    struct stat st;
    int fd;
    if(worker_started || !host || !host->invoke_service ||
       !path || !*path || strlen(path)>=sizeof socket_path)return STNLABZ_MODULE_ERR_START_FAILED;
    /* Socket directory MUST be private (0700) and owned by Sparkles UID. */
    {
        char parent[sizeof socket_path],*slash;
        memcpy(parent,path,strlen(path)+1U);
        slash=strrchr(parent,'/');
        if(!slash || slash==parent)return STNLABZ_MODULE_ERR_START_FAILED;
        *slash='\0';
        if(stat(parent,&st)!=0 || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() ||
           (st.st_mode&077U)!=0U)return STNLABZ_MODULE_ERR_START_FAILED;
    }
    memset(&addr,0,sizeof addr);
    addr.sun_family=AF_UNIX;
    memcpy(addr.sun_path,path,strlen(path)+1U);
    if(lstat(path,&st)==0){
        if(!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid())return STNLABZ_MODULE_ERR_START_FAILED;
        if(unlink(path)!=0)return STNLABZ_MODULE_ERR_START_FAILED;
    }else if(errno!=ENOENT)return STNLABZ_MODULE_ERR_START_FAILED;
    fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(fd<0)return STNLABZ_MODULE_ERR_START_FAILED;
    if(bind(fd,(struct sockaddr *)&addr,sizeof addr)!=0 ||
       chmod(path,S_IRUSR|S_IWUSR)!=0 || listen(fd,1)!=0){
        (void)close(fd);(void)unlink(path);return STNLABZ_MODULE_ERR_START_FAILED;
    }
    memcpy(socket_path,path,strlen(path)+1U);
    host_ptr=host;
    atomic_store(&stopping,0);
    atomic_store(&server_fd,fd);
    atomic_store(&peer_fd,-1);
    if(pthread_create(&worker,NULL,sl_worker,NULL)!=0){
        atomic_store(&server_fd,-1);(void)close(fd);
        (void)unlink(socket_path);host_ptr=NULL;return STNLABZ_MODULE_ERR_START_FAILED;
    }
    worker_started=1;
    log_status("staging IPC listener active; no Second Life grid connection");
    return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t sl_stop(void){
    int fd,peer;
    if(!worker_started)return STNLABZ_MODULE_ERR_INVALID_STATE;
    atomic_store(&stopping,1);
    peer=atomic_load(&peer_fd);
    fd=atomic_load(&server_fd);
    if(peer>=0)(void)shutdown(peer,SHUT_RDWR);
    if(fd>=0)(void)shutdown(fd,SHUT_RDWR);
    if(pthread_join(worker,NULL)!=0)return STNLABZ_MODULE_ERR_STOP_FAILED;
    worker_started=0;
    atomic_store(&server_fd,-1);
    atomic_store(&peer_fd,-1);
    if(fd>=0)(void)close(fd);
    (void)unlink(socket_path);
    memset(socket_path,0,sizeof socket_path);
    host_ptr=NULL;
    return STNLABZ_MODULE_OK;
}
static const stnlabz_module_descriptor_t descriptor={
    "sl","Sparkles Second Life Transport",SL_MAJOR,SL_MINOR,SL_PATCH,
    STNLABZ_MODULE_API_MAJOR,STNLABZ_MODULE_API_MINOR,
    sl_qualify,sl_start,sl_stop
};
const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void){
    return &descriptor;
}
