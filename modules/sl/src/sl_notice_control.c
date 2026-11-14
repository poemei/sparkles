#define _POSIX_C_SOURCE 200809L
#include "sl_notice_control.h"
#include "sparkles_sl_group_db.h"
#include "sparkles_llm.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NOTICE_CONFIG "/opt/sparkles/auth/sl/notice.conf"
#define NOTICE_PORT 17777
#define NOTICE_HTTP_LIMIT 8192
#define NOTICE_TTL 600
enum {NOTICE_IDLE, NOTICE_DRAFTING, NOTICE_READY, NOTICE_FAILED, NOTICE_QUEUED};
typedef struct {
 char token[129],operator_uuid[37],object_uuid[37];
} notice_auth_t;
typedef struct {
 pthread_mutex_t lock;
 pthread_t composer;
 int composer_started,phase;
 time_t expiry;
 char request_id[48],group[64],message[321],location[192];
 char error[96];
 sparkles_sl_notice_send_request_t action;
 sparkles_notice_control_t *control;
 notice_auth_t auth;
} notice_state_t;
static notice_state_t state={.lock=PTHREAD_MUTEX_INITIALIZER};

static char *find_header(char *buffer,const char *needle)
{
 size_t size=strlen(needle);
 char *p;
 for(p=buffer;*p;++p)
  if(!strncasecmp(p,needle,size))return p;
 return NULL;
}
static int uuid_ok(const char *s)
{
 size_t i;
 if(!s||strlen(s)!=36U)return 0;
 for(i=0;i<36U;++i) {
  char c=s[i];
  if(i==8U||i==13U||i==18U||i==23U) {if(c!='-')return 0;}
  else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 0;
 }
 return 1;
}
static int load_auth(notice_auth_t *out)
{
 char line[256],key[40],value[180];
 unsigned flags=0U;
 FILE *f=NULL;
 struct stat info;
 int fd=open(NOTICE_CONFIG,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0)return 0;
 if(fstat(fd,&info)||!S_ISREG(info.st_mode)||
    info.st_uid!=geteuid()||(info.st_mode&077U)||
    info.st_size<80||info.st_size>1024) {close(fd);return 0;}
 f=fdopen(fd,"r");
 if(!f){close(fd);return 0;}
 memset(out,0,sizeof *out);
 while(fgets(line,sizeof line,f)) {
  size_t n=strlen(line);
  if(!n||(line[n-1U]!='\n'&&!feof(f)))goto fail;
  if(sscanf(line,"%39[^=]=%179[^\r\n]",key,value)!=2)goto fail;
  if(!strcmp(key,"token") && !(flags&1U) &&
     strlen(value)>=32U&&strlen(value)<sizeof out->token) {
   strcpy(out->token,value);flags|=1U;
  }else if(!strcmp(key,"operator_uuid") && !(flags&2U)&&uuid_ok(value)) {
   strcpy(out->operator_uuid,value);flags|=2U;
  }else if(!strcmp(key,"object_uuid") && !(flags&4U)&&uuid_ok(value)) {
   strcpy(out->object_uuid,value);flags|=4U;
  }else goto fail;
 }
 if(ferror(f)||flags!=7U)goto fail;
 fclose(f);
 return 1;
fail:
 fclose(f);
 memset(out,0,sizeof *out);
 return 0;
}
static int constant_equal(const char *a,const char *b)
{
 size_t i,na=strlen(a),nb=strlen(b);
 unsigned diff=(unsigned)(na^nb);
 size_t n=na>nb?na:nb;
 if(n>128U)return 0;
 for(i=0U;i<n;++i)
  diff|=(unsigned char)(i<na?a[i]:0)^(unsigned char)(i<nb?b[i]:0);
 return diff==0U;
}
static int decode_value(const char *src,size_t len,char *dest,size_t cap,int multiline)
{
 size_t i,j=0U;
 if(!len||len>=cap)return 0;
 for(i=0U;i<len;++i) {
  unsigned char c=(unsigned char)src[i];
  if(c=='+')c=' ';
  else if(c=='%') {
   unsigned int h,l;
   char a,b;
   if(i+2U>=len)return 0;
   a=src[++i];b=src[++i];
   h=(a>='0'&&a<='9')?(unsigned)(a-'0'):
     (a>='A'&&a<='F')?(unsigned)(a-'A'+10):
     (a>='a'&&a<='f')?(unsigned)(a-'a'+10):256U;
   l=(b>='0'&&b<='9')?(unsigned)(b-'0'):
     (b>='A'&&b<='F')?(unsigned)(b-'A'+10):
     (b>='a'&&b<='f')?(unsigned)(b-'a'+10):256U;
   if(h>15U||l>15U)return 0;
   c=(unsigned char)((h<<4U)|l);
  }
  if((c<32U && !(multiline && c==10U))||c==127U||j+1U>=cap)return 0;
  dest[j++]=(char)c;
 }
 dest[j]=0;
 return j>0U;
}
static int form_get_mode(const char *body,const char *name,char *out,size_t cap,int multiline)
{
 const char *p=body;
 size_t name_len=strlen(name);
 int seen=0;
 while(*p) {
  const char *end=strchr(p,'&'),*eq;
  size_t len=end?(size_t)(end-p):strlen(p);
  eq=memchr(p,'=',len);
  if(eq && (size_t)(eq-p)==name_len &&
     !memcmp(p,name,name_len)) {
   if(seen||!decode_value(eq+1,(size_t)((p+len)-(eq+1)),out,cap,multiline))
    return 0;
   seen=1;
  }
  if(!end)break;
  p=end+1U;
 }
 return seen;
}
static int form_get(const char *body,const char *name,char *out,size_t cap)
{
 return form_get_mode(body,name,out,cap,0);
}
static int form_get_text(const char *body,const char *name,char *out,size_t cap)
{
 return form_get_mode(body,name,out,cap,1);
}
static int json_quote(const char *src,char *dest,size_t cap)
{
 size_t n=0U;
 while(*src) {
  unsigned char c=(unsigned char)*src++;
  if(c<32U||c==127U)return 0;
  if(c=='"'||c=='\\') {
   if(n+2U>=cap)return 0;
   dest[n++]='\\';dest[n++]=(char)c;
  }else{
   if(n+1U>=cap)return 0;
   dest[n++]=(char)c;
  }
 }
 dest[n]=0;return 1;
}
static int parse_draft(const char *raw,const char *group,
                       sparkles_sl_notice_send_request_t *a)
{
 const char *subject,*body;
 size_t gl,sl,bl,i;
 if(strncmp(raw,"GROUP=",6U))return 0;
 subject=strstr(raw,"\nSUBJECT=");
 body=strstr(raw,"\nBODY=");
 if(!subject||!body||body<=subject)return 0;
 gl=(size_t)(subject-(raw+6U));subject+=9U;
 sl=(size_t)(body-subject);body+=6U;
 bl=strlen(body);if(bl&&body[bl-1U]=='\n')--bl;
 if(gl!=strlen(group)||strncmp(raw+6U,group,gl)||!sl||sl>64U||
    !bl||bl>448U||sl+bl+1U>510U||
    memchr(subject,'\r',sl)||memchr(body,'\r',bl)||
    memchr(body,'\n',bl))return 0;
 for(i=0U;i<sl;++i)
  if((unsigned char)subject[i]<32U||subject[i]=='|')return 0;
 for(i=0U;i<bl;++i)
  if((unsigned char)body[i]<32U||body[i]=='|')return 0;
 memset(a,0,sizeof *a);
 memcpy(a->group,group,gl+1U);
 memcpy(a->subject,subject,sl);
 memcpy(a->body,body,bl);
 if(snprintf(a->account,sizeof a->account,"sl:%s",
             state.auth.operator_uuid)>= (int)sizeof a->account)return 0;
 return 1;
}
static int group_allowed(const sparkles_sl_notice_send_request_t *a)
{
 sparkles_sl_group_resolve_request_t in={0};
 sparkles_sl_group_resolve_reply_t out={0};
 size_t used=0U;
 memcpy(in.account,a->account,strlen(a->account)+1U);
 memcpy(in.group_name,a->group,strlen(a->group)+1U);
 in.notice=1U;
 return state.control->host->invoke_service(
   SPARKLES_SL_GROUP_RESOLVE_SERVICE,&in,sizeof in,&out,sizeof out,
   &used)==STNLABZ_MODULE_OK && used==sizeof out && out.matched;
}
static void *compose(void *unused)
{
 sparkles_llm_request_t in={{0}};
 sparkles_llm_response_t out={{0}};
 sparkles_sl_notice_send_request_t a={0};
 char group[64],message[321],location[192],request_id[48];
 size_t used=0U;
 int n,ok=0;
 (void)unused;
 pthread_mutex_lock(&state.lock);
 strcpy(group,state.group);strcpy(message,state.message);
 strcpy(location,state.location);strcpy(request_id,state.request_id);
 pthread_mutex_unlock(&state.lock);
 n=snprintf(in.prompt,sizeof in.prompt,
  "You are Sparkles, drafting an official Second Life group notice from "
  "an authorized event box. This is a structured production request, "
  "NOT a casual IM. Use only supplied facts; do not invent a date or "
  "venue. Output EXACTLY three lines with no explanation or markdown:\n"
  "GROUP=%s\nSUBJECT=<maximum 64 characters>\n"
  "BODY=<maximum 320 characters including the supplied location URL>\n"
  "Include the location URL in the body exactly as given.\n"
  "Event details: %s\nLocation: %s\n",group,message,location);
 if(n>0&&(size_t)n<sizeof in.prompt &&
    state.control->host->invoke_service(
     SPARKLES_LLM_GENERATE_SERVICE,&in,sizeof in,&out,sizeof out,
     &used)==STNLABZ_MODULE_OK && used==sizeof out &&
    parse_draft(out.text,group,&a) && strstr(a.body,location) &&
    group_allowed(&a))ok=1;
 pthread_mutex_lock(&state.lock);
 if(!strcmp(request_id,state.request_id)&&state.phase==NOTICE_DRAFTING &&
    !atomic_load(state.control->stop)) {
  if(ok){state.action=a;state.phase=NOTICE_READY;}
  else {state.phase=NOTICE_FAILED;
   strcpy(state.error,"Drafting or group validation failed");}
 }
 pthread_mutex_unlock(&state.lock);
 return NULL;
}
static void send_json(int fd,int status,const char *body)
{
 char header[256];
 int n=snprintf(header,sizeof header,
  "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
  "Cache-Control: no-store\r\nConnection: close\r\n"
  "Content-Length: %zu\r\n\r\n",
  status,status==200?"OK":status==202?"Accepted":
  status==401?"Unauthorized":status==409?"Conflict":
  status==503?"Service Unavailable":"Bad Request",strlen(body));
 if(n>0&&(size_t)n<sizeof header) {
  (void)send(fd,header,(size_t)n,MSG_NOSIGNAL);
  (void)send(fd,body,strlen(body),MSG_NOSIGNAL);
 }
}
static void respond(int fd,const char *op,const char *body)
{
 char id[48]={0},group[64]={0},message[321]={0},
      location[192]={0},owner[37]={0},object[37]={0};
 char sb[160]={0},bb[920]={0},reply[1400];
 int phase,ok;
 time_t now=time(NULL);
 if(!form_get(body,"request_id",id,sizeof id)) {
  send_json(fd,400,"{\"error\":\"missing request id\"}");return;
 }
 /* Manual marketing HUD: exact text, bounded MariaDB destinations, no LLM.
  * Only the restricted, token-authenticated intake may call this operation.
  * This produces queue receipts, not simulator publication receipts. */
 if(!strcmp(op,"post")) {
  static const char signature[]=
      "~~ This message generated by Sparkles Entertainment ~~";
  char title[65]={0},content[449]={0},venue_name[128]={0};
  char notice_body[449]={0},account[128]={0};
  sparkles_sl_marketing_groups_request_t list_request={0};
  sparkles_sl_marketing_groups_reply_t destinations={0};
  size_t list_used=0U,i,queued=0U,body_len;
  stnlabz_module_result_t list_result;
  if(!form_get(body,"title",title,sizeof title)||
     !form_get_text(body,"message",content,sizeof content)||
     !form_get(body,"venue",venue_name,sizeof venue_name)||
     !form_get(body,"location",location,sizeof location)||
     !form_get(body,"operator_uuid",owner,sizeof owner)||
     !form_get(body,"object_uuid",object,sizeof object)||
     strcmp(owner,state.auth.operator_uuid)||
     strcmp(object,state.auth.object_uuid)||
     strncmp(location,"https://maps.secondlife.com/secondlife/",39U)) {
    send_json(fd,400,"{\"error\":\"invalid notice fields\"}");return;
  }
  for(i=0U;title[i];++i)
    if(title[i]=='|'||(unsigned char)title[i]<32U) {
      send_json(fd,400,"{\"error\":\"invalid title\"}");return;
    }
  for(i=0U;content[i];++i)
    if(content[i]=='|'||content[i]=='\r'||
       ((unsigned char)content[i]<32U&&content[i]!='\n')) {
      send_json(fd,400,"{\"error\":\"invalid notice body\"}");return;
    }
  if(snprintf(account,sizeof account,"sl:%s",owner)>=(int)sizeof account ||
     snprintf(notice_body,sizeof notice_body,
         "%s\n\nYour Ride Awaits -> %s\n\n%s",
         content,location,signature)>=(int)sizeof notice_body ||
     strlen(title)+strlen(notice_body)+1U>510U) {
    send_json(fd,400,"{\"error\":\"notice too long; nothing queued\"}");return;
  }
  body_len=strlen(notice_body);
  pthread_mutex_lock(&state.lock);
  if(!strcmp(state.request_id,id) ||
     (state.phase!=NOTICE_IDLE&&now<state.expiry) ||
     state.composer_started) {
    pthread_mutex_unlock(&state.lock);
    send_json(fd,409,"{\"error\":\"duplicate or active request\"}");return;
  }
  memset(&state.action,0,sizeof state.action);
  memcpy(state.request_id,id,strlen(id)+1U);
  state.phase=NOTICE_QUEUED;
  state.expiry=now+NOTICE_TTL;
  pthread_mutex_unlock(&state.lock);
  memcpy(list_request.account,account,strlen(account)+1U);
  list_result=state.control->host->invoke_service(
      SPARKLES_SL_MARKETING_GROUPS_SERVICE,
      &list_request,sizeof list_request,&destinations,
      sizeof destinations,&list_used);
  if(list_result==STNLABZ_MODULE_OK &&
     list_used==sizeof destinations &&
     destinations.count>0U &&
     destinations.count<=SPARKLES_SL_MARKETING_GROUPS_MAX) {
    for(i=0U;i<destinations.count;++i) {
      sparkles_sl_notice_send_request_t notice={0};
      size_t name_length=strnlen(destinations.names[i],
                                sizeof destinations.names[i]);
      if(name_length==0U||name_length>=sizeof notice.group)break;
      memcpy(notice.account,account,strlen(account)+1U);
      memcpy(notice.group,destinations.names[i],name_length+1U);
      memcpy(notice.subject,title,strlen(title)+1U);
      memcpy(notice.body,notice_body,body_len+1U);
      if(!group_allowed(&notice) ||
         !state.control->publish(&notice,state.control->publish_ctx))break;
      ++queued;
    }
  }
  if(queued==0U||queued!=destinations.count) {
    pthread_mutex_lock(&state.lock);
    if(state.phase==NOTICE_QUEUED)state.phase=NOTICE_FAILED;
    pthread_mutex_unlock(&state.lock);
  }
  snprintf(reply,sizeof reply,
    "{\"status\":\"%s\",\"queued\":%zu,\"total\":%u,"
    "\"publication_confirmed\":false}",
    queued==destinations.count && queued>0U?"queued":
    queued?"partial":"failed",queued,destinations.count);
  send_json(fd,queued==destinations.count && queued>0U?200:503,reply);
  return;
 }
 if(!strcmp(op,"draft")) {
  if(!form_get(body,"group",group,sizeof group)||
     !form_get(body,"message",message,sizeof message)||
     !form_get(body,"location",location,sizeof location)||
     !form_get(body,"operator_uuid",owner,sizeof owner)||
     !form_get(body,"object_uuid",object,sizeof object)||
     strcmp(owner,state.auth.operator_uuid)||
     strcmp(object,state.auth.object_uuid)||
     strncmp(location,"https://maps.secondlife.com/secondlife/",39U)) {
   send_json(fd,400,"{\"error\":\"invalid request fields\"}");return;
  }
  pthread_mutex_lock(&state.lock);
  if(state.phase!=NOTICE_IDLE && now<state.expiry) {
   pthread_mutex_unlock(&state.lock);
   send_json(fd,409,"{\"error\":\"another request is active\"}");return;
  }
  if(state.composer_started) {
   pthread_mutex_unlock(&state.lock);
   /* Previous draft worker may still own the inference call. */
   send_json(fd,409,"{\"error\":\"prior draft worker active\"}");return;
  }
  memset(&state.action,0,sizeof state.action);
  strcpy(state.request_id,id);strcpy(state.group,group);
  strcpy(state.message,message);strcpy(state.location,location);
  state.phase=NOTICE_DRAFTING;state.expiry=now+NOTICE_TTL;
  ok=pthread_create(&state.composer,NULL,compose,NULL)==0;
  if(ok)state.composer_started=1;
  else state.phase=NOTICE_FAILED;
  pthread_mutex_unlock(&state.lock);
  send_json(fd,ok?202:503,ok?
    "{\"status\":\"drafting\"}":"{\"error\":\"draft worker unavailable\"}");
  return;
 }
 pthread_mutex_lock(&state.lock);
 phase=state.phase;
 if(strcmp(state.request_id,id) || now>=state.expiry) {
  pthread_mutex_unlock(&state.lock);
  send_json(fd,409,"{\"error\":\"unknown or expired request\"}");return;
 }
 if(!strcmp(op,"status")) {
  if(phase==NOTICE_READY) {
   if(!json_quote(state.action.subject,sb,sizeof sb) ||
      !json_quote(state.action.body,bb,sizeof bb)) {
    pthread_mutex_unlock(&state.lock);
    send_json(fd,503,"{\"error\":\"unsafe draft\"}");return;
   }
   snprintf(reply,sizeof reply,
    "{\"status\":\"ready\",\"subject\":\"%s\",\"body\":\"%s\"}",sb,bb);
  }else snprintf(reply,sizeof reply,"{\"status\":\"%s\"}",
      phase==NOTICE_DRAFTING?"drafting":phase==NOTICE_FAILED?"failed":
      phase==NOTICE_QUEUED?"queued":"idle");
  pthread_mutex_unlock(&state.lock);
  send_json(fd,200,reply);return;
 }
 if(!strcmp(op,"publish")&&phase==NOTICE_READY) {
  sparkles_sl_notice_send_request_t action=state.action;
  /* Consume approval before attempting the network queue. An uncertain
   * result cannot be repeated with the same request identifier. */
  state.phase=NOTICE_QUEUED;
  pthread_mutex_unlock(&state.lock);
  ok=group_allowed(&action) &&
     state.control->publish(&action,state.control->publish_ctx);
  if(!ok) {
   pthread_mutex_lock(&state.lock);
   if(state.phase==NOTICE_QUEUED)state.phase=NOTICE_FAILED;
   pthread_mutex_unlock(&state.lock);
  }
  send_json(fd,ok?200:503,ok?
   "{\"status\":\"queued\",\"publication_confirmed\":false}":
   "{\"status\":\"failed\",\"publication_confirmed\":false}");
  return;
 }
 if(!strcmp(op,"cancel")&&phase!=NOTICE_QUEUED) {
  state.phase=NOTICE_IDLE;
  pthread_mutex_unlock(&state.lock);
  send_json(fd,200,"{\"status\":\"cancelled\"}");return;
 }
 pthread_mutex_unlock(&state.lock);
 send_json(fd,409,"{\"error\":\"invalid request state\"}");
}
static void handle_client(int fd)
{
 char raw[NOTICE_HTTP_LIMIT+1U]={0};
 char token[129]={0},op[32]={0};
 size_t total=0U,header_len=0U,content_len=0U;
 char *end=NULL,*body=NULL,*line;
 ssize_t n;
 struct timeval timeout={3,0};
 (void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 while(total<NOTICE_HTTP_LIMIT) {
  n=recv(fd,raw+total,NOTICE_HTTP_LIMIT-total,0);
  if(n<=0)break;
  total+=(size_t)n;raw[total]=0;
  end=strstr(raw,"\r\n\r\n");
  if(end) {
   header_len=(size_t)(end+4U-raw);
   line=find_header(raw,"\nContent-Length:");
   if(!line||line>=end)break;
   content_len=strtoul(line+16U,NULL,10);
   if(content_len>4096U||!content_len)break;
   if(header_len+content_len<=total) {
    body=raw+header_len;
    body[content_len]=0;
    break;
   }
  }
 }
 if(!body||strncmp(raw,"POST /notice HTTP/1.1\r\n",23U)) {
  send_json(fd,400,"{\"error\":\"invalid HTTP request\"}");return;
 }
 line=find_header(raw,"\nX-Sparkles-Object-Token:");
 if(!line||line>=end) {
  send_json(fd,401,"{\"error\":\"unauthorized\"}");return;
 }
 line+=25U;
 while(*line==' ')++line;
 n=0;
 while(line[n]&&line[n]!='\r'&&line[n]!='\n'&&n<128)++n;
 memcpy(token,line,(size_t)n);token[n]=0;
 if(!constant_equal(token,state.auth.token)) {
  send_json(fd,401,"{\"error\":\"unauthorized\"}");return;
 }
 if(!form_get(body,"op",op,sizeof op)) {
  send_json(fd,400,"{\"error\":\"missing operation\"}");return;
 }
 respond(fd,op,body);
}
int sparkles_notice_control_run(sparkles_notice_control_t *control)
{
 int fd=-1,one=1,client;
 struct sockaddr_in address={0};
 struct pollfd poller;
 notice_auth_t auth={0};
 if(!control||!control->stop||!control->host||!control->publish)
  return 0;
 if(!load_auth(&auth)) {
  fprintf(stderr,"[SL NOTICE] disabled: missing/invalid 0600 notice.conf\n");
  return 0;
 }
 pthread_mutex_lock(&state.lock);
 state.control=control;state.auth=auth;
 pthread_mutex_unlock(&state.lock);
 fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
 if(fd<0)return 0;
 (void)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
 address.sin_family=AF_INET;address.sin_port=htons(NOTICE_PORT);
 address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(bind(fd,(struct sockaddr *)&address,sizeof address)||listen(fd,4)) {
  close(fd);return 0;
 }
 fprintf(stderr,"[SL NOTICE] restricted intake listening on 127.0.0.1:%d\n",NOTICE_PORT);
 poller.fd=fd;poller.events=POLLIN;
 while(!atomic_load(control->stop)) {
  if(poll(&poller,1,500)<=0)continue;
  client=accept(fd,NULL,NULL);
  if(client<0)continue;
  handle_client(client);
  close(client);
  /* Reap any completed draft before accepting another. */
  pthread_mutex_lock(&state.lock);
  if(state.composer_started&&state.phase!=NOTICE_DRAFTING) {
   pthread_t finished=state.composer;
   state.composer_started=0;
   pthread_mutex_unlock(&state.lock);
   pthread_join(finished,NULL);
  }else pthread_mutex_unlock(&state.lock);
 }
 close(fd);
 pthread_mutex_lock(&state.lock);
 if(state.composer_started) {
  pthread_t finished=state.composer;
  pthread_mutex_unlock(&state.lock);
  pthread_join(finished,NULL);
 }else pthread_mutex_unlock(&state.lock);
 memset(&state.auth,0,sizeof state.auth);
 return 1;
}
