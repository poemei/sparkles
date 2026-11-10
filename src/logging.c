#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "logging.h"

static FILE *stnlabz_log_stream;
static FILE *module_log_stream;
static unsigned long module_sequence;

static int open_pair(const char *core_path,const char *module_path)
{
    if(stnlabz_log_stream||module_log_stream||!core_path||!module_path)return 0;
    stnlabz_log_stream=fopen(core_path,"a");
    if(!stnlabz_log_stream)return 0;
    module_log_stream=fopen(module_path,"a");
    if(!module_log_stream){
        (void)fclose(stnlabz_log_stream);stnlabz_log_stream=NULL;
        return 0;
    }
    (void)setvbuf(stnlabz_log_stream,NULL,_IOLBF,0);
    (void)setvbuf(module_log_stream,NULL,_IOLBF,0);
    return 1;
}
int stnlabz_log_is_open(void){return stnlabz_log_stream&&module_log_stream;}

int stnlabz_log_open_path(const char *path)
{
    char module_path[1024];
    const char *slash;
    int n;
    if(!path||path[0]!='/'||!(slash=strrchr(path,'/')))return 0;
    n=snprintf(module_path,sizeof module_path,"%.*s/modules.log",
        (int)(slash-path),path);
    if(n<0||(size_t)n>=sizeof module_path)return 0;
    return open_pair(path,module_path);
}
int stnlabz_log_open(void)
{
    if(mkdir(STNLABZ_LOG_DIRECTORY,0700)!=0&&errno!=EEXIST)return 0;
    return open_pair(STNLABZ_LOG_PATH,STNLABZ_LEGACY_MODULE_LOG_PATH);
}
static int is_module_component(const char *component)
{
    return !strcmp(component,"MODULE")||!strcmp(component,"DISCOVERY")||
        !strcmp(component,"ACTIVATION");
}
/* Operator module log contract: meaningful lifecycle transitions and messages.
   Routine inspection/request/stop chatter is not an operator-facing finding. */
static int routine_module_event(const char *event){
 static const char *const names[]={
  "VERIFY_ATTEMPT","VERIFY_PASS","QUALIFY_ATTEMPT",
  "AUTHORIZE_REQUEST","AUTHORIZE_GRANTED","ACTIVATE_REQUEST",
  "STOP_REQUEST","STOP_PASS","SCAN_START","SCAN_COMPLETE",
  "DIRECTORY_CREATED","MANAGER_SHUTDOWN"
 };
 size_t i;
 for(i=0;i<sizeof names/sizeof names[0];i++)if(!strcmp(event,names[i]))return 1;
 return 0;
}
static void module_identity(const char *detail,char *id,size_t id_size,
 char *version,size_t version_size){
 const char *p;size_t n;
 snprintf(id,id_size,"unknown");version[0]=0;
 if(!detail)return;
 p=strstr(detail,"module=");
 if(!p)p=strstr(detail,"id=");
 if(p){
  p+=p[0]=='m'?7:3;
  n=strcspn(p," \r\n");
  if(n>0&&n<id_size){memcpy(id,p,n);id[n]=0;}
 }
 p=strstr(detail,"version=");
 if(p){
  p+=8;n=strcspn(p," \r\n");
  if(n>0&&n<version_size){memcpy(version,p,n);version[n]=0;}
 }
}
int stnlabz_log_event(const char *component,const char *event,const char *detail)
{
    time_t now;
    struct tm utc;
    char stamp[32];
    FILE *destination;
    int written;
    if(!stnlabz_log_is_open()||!component||!event)return 0;
    destination=is_module_component(component)?module_log_stream:stnlabz_log_stream;
    now=time(NULL);
    if(now==(time_t)-1||!gmtime_r(&now,&utc)||
       !strftime(stamp,sizeof stamp,"%Y-%m-%dT%H:%M:%SZ",&utc))return 0;
    if(destination==module_log_stream){
        char id[80],version[48];
        if(routine_module_event(event))return 1;
        if(!strcmp(event,"MESSAGE")){
            written=fprintf(destination,"%s MODULE MESSAGE %s\n",stamp,detail?detail:"");
        }else{
            module_identity(detail,id,sizeof id,version,sizeof version);
            module_sequence++;
            int failure=strstr(event,"FAILED")||strstr(event,"REJECT")||
                strstr(event,"DENIED")||strstr(event,"ERROR");
            written=fprintf(destination,"%s MODULE %s sequence=%lu module=%s%s%s%s%s\n",
                stamp,event,module_sequence,id,version[0]?" version=":"",version,
                failure&&detail?" evidence=":"",failure&&detail?detail:"");
        }
    }else{
        written=fprintf(destination,"%s %s %s%s%s\n",
            stamp,component,event,detail&&*detail?" ":"",detail&&*detail?detail:"");
    }
    return written>0&&!ferror(destination);
}
int stnlabz_log_sync(void)
{
    return stnlabz_log_is_open()&&fflush(stnlabz_log_stream)==0&&
        fsync(fileno(stnlabz_log_stream))==0&&
        fflush(module_log_stream)==0&&fsync(fileno(module_log_stream))==0;
}
void stnlabz_log_close(void)
{
    if(stnlabz_log_stream){
        (void)fflush(stnlabz_log_stream);(void)fclose(stnlabz_log_stream);
        stnlabz_log_stream=NULL;
    }
    if(module_log_stream){
        (void)fflush(module_log_stream);(void)fclose(module_log_stream);
        module_log_stream=NULL;
    }
}
