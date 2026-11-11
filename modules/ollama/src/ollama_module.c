#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "stnlabz_abi/module.h"
#include "sparkles_llm.h"
#include "sparkles_identity.h"
#include "sparkles_mariadb.h"
#include "ollama_schedule.h"
#include "ollama_internal.h"

#define OLLAMA_VERSION_MAJOR 1U
#define OLLAMA_VERSION_MINOR 0U
#define OLLAMA_VERSION_PATCH 15U

typedef struct { char *data; size_t used; size_t capacity; } buffer_t;
static const stnlabz_module_host_t *owner;
static int service_registered;
static int command_registered;
static stnlabz_module_result_t generate(const void *,size_t,void *,size_t,size_t *,void *);
static const int schedule_extraction_mode=1;
static int calendar_request(const char *text)
{
    static const char *const terms[]={
        "schedule","calendar","appointment","availability","conflict"
    };
    char lower[701];
    size_t i,j,n;
    if(!text)return 0;
    n=strlen(text);
    if(n>=sizeof lower)return 0;
    for(i=0;i<n;++i) {
        unsigned char c=(unsigned char)text[i];
        lower[i]=(c>='A'&&c<='Z')?(char)(c+('a'-'A')):(char)c;
    }
    lower[n]='\0';
    for(j=0;j<sizeof terms/sizeof terms[0];++j)
        if(strstr(lower,terms[j]))return 1;
    return 0;
}


#define SPARKLES_PENDING_MAX 8U
#define SPARKLES_PENDING_SECONDS 600

typedef struct {
    char account[128];
    char sender[128];
    time_t expires;
    int waiting_for_date;
    sparkles_schedule_plan_t plan;
} pending_schedule_t;

static pending_schedule_t pending[SPARKLES_PENDING_MAX];
static pthread_mutex_t pending_lock=PTHREAD_MUTEX_INITIALIZER;

/* Only server-verified account names can create a conversation pending state. */
static int pending_find(const char *account,const char *sender,time_t now)
{
    unsigned int i;
    for(i=0U;i<SPARKLES_PENDING_MAX;++i) {
        if(pending[i].expires && pending[i].expires<=now)
            memset(&pending[i],0,sizeof pending[i]);
        if(pending[i].expires && !strcmp(pending[i].account,account) &&
           !strcmp(pending[i].sender,sender))return (int)i;
    }
    return -1;
}
static int pending_save(const char *account,const char *sender,
                        const sparkles_schedule_plan_t *plan,int needs_date)
{
    time_t now=time(NULL);
    int slot;
    unsigned int i;
    if(!account||!*account||!sender||!*sender||!plan||
       strlen(account)>=sizeof pending[0].account||
       strlen(sender)>=sizeof pending[0].sender)return 0;
    (void)pthread_mutex_lock(&pending_lock);
    slot=pending_find(account,sender,now);
    if(slot<0)for(i=0U;i<SPARKLES_PENDING_MAX;++i)
        if(!pending[i].expires){slot=(int)i;break;}
    if(slot>=0) {
        memset(&pending[slot],0,sizeof pending[slot]);
        memcpy(pending[slot].account,account,strlen(account)+1U);
        memcpy(pending[slot].sender,sender,strlen(sender)+1U);
        pending[slot].expires=now+SPARKLES_PENDING_SECONDS;
        pending[slot].waiting_for_date=needs_date;
        pending[slot].plan=*plan;
    }
    (void)pthread_mutex_unlock(&pending_lock);
    return slot>=0;
}
static void pending_clear(const char *account,const char *sender)
{
    int slot;
    (void)pthread_mutex_lock(&pending_lock);
    slot=pending_find(account,sender,time(NULL));
    if(slot>=0)memset(&pending[slot],0,sizeof pending[slot]);
    (void)pthread_mutex_unlock(&pending_lock);
}
static int schedule_reply(stnlabz_module_command_reply_fn reply,void *ctx,
                          const char *text)
{
    return reply(ctx,text)?1:0;
}
static int send_schedule_summary(const char *sender,
                                 const sparkles_schedule_plan_t *plan)
{
    char message[512];
    size_t used=0U;
    unsigned int i;
    int n;
    if(!owner||!owner->send_private_message)return 0;
    n=snprintf(message,sizeof message,"Please confirm these appointments: ");
    if(n<0 || (size_t)n>=sizeof message)return 0;
    used=(size_t)n;
    for(i=0U;i<plan->count;++i) {
        const sparkles_schedule_item_t *e=&plan->items[i];
        n=snprintf(message+used,sizeof message-used,"%s%s %s-%s %s",
            i?"; ":"",e->date,e->begin,e->end,e->title);
        if(n<0 || (size_t)n>=sizeof message-used)return 0;
        used+=(size_t)n;
    }
    n=snprintf(message+used,sizeof message-used,
               ". Reply 'confirm' to save, or 'cancel'.");
    if(n<0 || (size_t)n>=sizeof message-used)return 0;
    return owner->send_private_message(sender,message);
}
static int find_explicit_date(const char *text,char date[11])
{
    size_t i,n;
    if(!text)return 0;
    n=strlen(text);
    for(i=0U;i+10U<=n;++i) {
        char sample[11];
        if(i>0U && text[i-1U]>='0' && text[i-1U]<='9')continue;
        if(i+10U<n && text[i+10U]>='0' && text[i+10U]<='9')continue;
        memcpy(sample,text+i,10U);
        sample[10]='\0';
        if(sparkles_schedule_date(sample,date))return 1;
    }
    return 0;
}
/* Authoritative pre-confirmation conflict check; never model arithmetic. */
static int check_schedule_plan(const sparkles_schedule_plan_t *plan,
                               const char *account)
{
    unsigned int i,j;
    if(!owner||!owner->invoke_service)return -2;
    for(i=0U;i<plan->count;++i) {
        const sparkles_schedule_item_t *e=&plan->items[i];
        sparkles_mariadb_create_request_t req={{0}};
        sparkles_mariadb_check_reply_t out={0};
        size_t used=0U;
        stnlabz_module_result_t rc;
        for(j=0U;j<i;++j) {
            const sparkles_schedule_item_t *prev=&plan->items[j];
            if(!strcmp(e->date,prev->date) &&
               strcmp(e->begin,prev->end)<0 &&
               strcmp(prev->begin,e->end)<0)
                return -1;
        }
        if(snprintf(req.start_time,sizeof req.start_time,"%s %s:00",
                    e->date,e->begin)!=19 ||
           snprintf(req.end_time,sizeof req.end_time,"%s %s:00",
                    e->date,e->end)!=19)return -2;
        memcpy(req.account,account,strlen(account)+1U);
        memcpy(req.title,e->title,strlen(e->title)+1U);
        rc=owner->invoke_service(SPARKLES_MARIADB_CHECK_SERVICE,
            &req,sizeof req,&out,sizeof out,&used);
        if(rc!=STNLABZ_MODULE_OK||used!=sizeof out)return -2;
        if(out.status==SPARKLES_MARIADB_CHECK_CONFLICT)return -1;
        if(out.status!=SPARKLES_MARIADB_CHECK_FREE)return -2;
    }
    return 1;
}
static int finish_schedule(const sparkles_schedule_plan_t *plan,
                           const char *account)
{
    unsigned int i;
    unsigned int saved=0U,conflicts=0U;
    int ambiguous=0;
    if(!owner||!owner->invoke_service)return 0;
    for(i=0U;i<plan->count;++i) {
        const sparkles_schedule_item_t *e=&plan->items[i];
        sparkles_mariadb_create_request_t request={{0}};
        sparkles_mariadb_create_reply_t result={0};
        size_t used=0U;
        stnlabz_module_result_t rc;
        if(snprintf(request.start_time,sizeof request.start_time,"%s %s:00",
                    e->date,e->begin)!=19 ||
           snprintf(request.end_time,sizeof request.end_time,"%s %s:00",
                    e->date,e->end)!=19)
            return 0;
        memcpy(request.account,account,strlen(account)+1U);
        memcpy(request.title,e->title,strlen(e->title)+1U);
        rc=owner->invoke_service(SPARKLES_MARIADB_CREATE_SERVICE,
            &request,sizeof request,&result,sizeof result,&used);
        if(rc!=STNLABZ_MODULE_OK||used!=sizeof result)return 0;
        if(result.status==SPARKLES_MARIADB_CREATE_SAVED)++saved;
        else if(result.status==SPARKLES_MARIADB_CREATE_CONFLICT)++conflicts;
        else if(result.status==SPARKLES_MARIADB_CREATE_UNCERTAIN)ambiguous=1;
        else return 0;
    }
    /* Caller reports counts without claiming success on uncertain outcome. */
    return ambiguous?-3:conflicts?(saved?-2:-1):(int)saved;
}

/* Returns 1 if this utterance is fully handled as a scheduling turn. */
static int schedule_conversation(const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,void *ctx)
{
    sparkles_schedule_plan_t plan={0};
    int slot;
    int waiting=0;
    time_t now=time(NULL);
    char date[11];
    char account[128],sender[128];
    char prompt[SPARKLES_LLM_PROMPT_MAX];
    sparkles_llm_request_t req={{0}};
    sparkles_llm_response_t out={{0}};
    size_t used=0U;
    int n;
    stnlabz_module_result_t rc;
    const char *text=command->arguments;
    if(!command->sender[0])return 0;
    if(strlen(command->account)>=sizeof account ||
       strlen(command->sender)>=sizeof sender)return 0;
    memcpy(account,command->account,strlen(command->account)+1U);
    memcpy(sender,command->sender,strlen(command->sender)+1U);
    (void)pthread_mutex_lock(&pending_lock);
    slot=pending_find(account,sender,now);
    if(slot>=0) {
        plan=pending[slot].plan;
        waiting=pending[slot].waiting_for_date;
    }
    (void)pthread_mutex_unlock(&pending_lock);
    if(slot>=0) {
        if(!strcmp(text,"cancel")||!strcmp(text,"no")) {
            (void)pthread_mutex_lock(&pending_lock);
            slot=pending_find(account,sender,time(NULL));
            if(slot>=0)memset(&pending[slot],0,sizeof pending[slot]);
            (void)pthread_mutex_unlock(&pending_lock);
            return schedule_reply(reply,ctx,"Appointment proposal cancelled.");
        }
        if(waiting) {
            unsigned int i;
            if(!find_explicit_date(text,date))
                return schedule_reply(reply,ctx,
                    "Which date? Please give the calendar date, e.g. 2026-11-11.");
            for(i=0U;i<plan.count;++i)
                if(!strcmp(plan.items[i].date,"UNKNOWN"))
                    memcpy(plan.items[i].date,date,11U);
            {
                int check=check_schedule_plan(&plan,account);
                if(check!=1) {
                    pending_clear(account,sender);
                    return schedule_reply(reply,ctx,
                        check==-1?"The proposed time conflicts with your calendar; nothing saved.":
                                  "Calendar conflict check unavailable; nothing saved.");
                }
            }
            if(!pending_save(account,sender,&plan,0))
                return schedule_reply(reply,ctx,"Schedule proposal unavailable.");
            if(!send_schedule_summary(sender,&plan)) {
                pending_clear(account,sender);
                return schedule_reply(reply,ctx,"Private confirmation unavailable.");
            }
            return schedule_reply(reply,ctx,
                "I sent your appointment details privately for confirmation.");
        }
        if(!strcmp(text,"confirm")||!strcmp(text,"yes")) {
            int result;
            /* Single use: clear pending before any network/database operation. */
            (void)pthread_mutex_lock(&pending_lock);
            slot=pending_find(account,sender,time(NULL));
            if(slot<0) {
                (void)pthread_mutex_unlock(&pending_lock);
                return schedule_reply(reply,ctx,"The appointment proposal expired.");
            }
            plan=pending[slot].plan;
            memset(&pending[slot],0,sizeof pending[slot]);
            (void)pthread_mutex_unlock(&pending_lock);
            result=finish_schedule(&plan,account);
            if(result==(int)plan.count)
                return schedule_reply(reply,ctx,
                    "Your confirmed appointments were saved in the calendar.");
            if(result==-1)
                return schedule_reply(reply,ctx,
                    "Nothing saved; the proposed times conflict with existing appointments.");
            if(result==-2)
                return schedule_reply(reply,ctx,
                    "Some appointments were saved; others conflicted. Check your schedule.");
            if(result==-3)
                return schedule_reply(reply,ctx,
                    "Database confirmation uncertain. Check your schedule before retrying.");
            return schedule_reply(reply,ctx,
                "Calendar save failed or incomplete. Check your schedule before retrying.");
        }
        /* A new message can supersede the old proposal, but never confirm it. */
        (void)pthread_mutex_lock(&pending_lock);
        slot=pending_find(account,sender,time(NULL));
        if(slot>=0)memset(&pending[slot],0,sizeof pending[slot]);
        (void)pthread_mutex_unlock(&pending_lock);
    }
    /* Model extracts intentions, but cannot write or make authoritative
     * decisions. Strict parsing and a separate human confirmation are required. */
    if(strlen(text)>700U)return 0;
    n=snprintf(prompt,sizeof prompt,
        "Extract at most 2 calendar events directly stated by the human. "
        "Output ONLY NONE if no proposed/specified event is given, or one "
        "EVENT|DATE|HH:MM|HH:MM|TITLE per line. DATE must be an explicit "
        "YYYY-MM-DD in the message; otherwise use UNKNOWN. "
        "Use 24-hour times. Never invent a date, time, venue or person. "
        "Do not add prose or formatting. For multiple events use two lines. "
        "Human: %s",text);
    if(n<=0||(size_t)n>=sizeof prompt)return 0;
    memcpy(req.prompt,prompt,(size_t)n+1U);
    rc=generate(&req,sizeof req,&out,sizeof out,&used,
                (void *)&schedule_extraction_mode);
    if(rc!=STNLABZ_MODULE_OK||used!=sizeof out||
       !sparkles_schedule_parse(out.text,&plan)) {
        fprintf(stderr,"[OLLAMA] calendar extraction failed or output invalid\\n");
        return calendar_request(text)
            ? schedule_reply(reply,ctx,
                "I couldn't reliably extract the appointments. Please repeat the details.")
            : 0;
    }
    if(!plan.count)
        return calendar_request(text)
            ? schedule_reply(reply,ctx,
                "I couldn't identify appointment times. Please clarify the event details.")
            : 0;
    if(!account[0])return schedule_reply(reply,ctx,
       "Calendar changes require a verified IRC account.");
    {
        unsigned int i;
        int needs_date=0;
        char explicit_date[11];
        int has_explicit=find_explicit_date(text,explicit_date);
        for(i=0U;i<plan.count;++i) {
            /* Ignore model-invented dates; only an explicitly supplied
             * ISO calendar date can be used. */
            if(!has_explicit || strcmp(plan.items[i].date,explicit_date))
                memcpy(plan.items[i].date,"UNKNOWN",8U);
            if(!strcmp(plan.items[i].date,"UNKNOWN"))needs_date=1;
        }
        if(!needs_date) {
            int check=check_schedule_plan(&plan,account);
            if(check!=1)return schedule_reply(reply,ctx,
                check==-1?"The proposed time conflicts with your calendar; nothing saved.":
                          "Calendar conflict check unavailable; nothing saved.");
        }
        if(!pending_save(account,sender,&plan,needs_date))
            return schedule_reply(reply,ctx,"Calendar proposal unavailable.");
        if(needs_date)
            return schedule_reply(reply,ctx,
                "I have your event details. Which date (YYYY-MM-DD)?");
        if(!send_schedule_summary(sender,&plan)) {
            pending_clear(account,sender);
            return schedule_reply(reply,ctx,"Private confirmation unavailable.");
        }
        return schedule_reply(reply,ctx,
            "I sent your appointment details privately for confirmation.");
    }
}


static int valid_peer_char(char c)
{
 return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
        c=='_'||c=='-'||c=='['||c==']'||c=='\\'||c=='`'||c=='^'||
        c=='{'||c=='}'||c=='|';
}

static int copy_target_token(const char *p,char *target,size_t target_size)
{
 size_t used=0U;
 while(*p==' '||*p=='\t')++p;
 while(*p&&valid_peer_char(*p)&&used+1U<target_size)target[used++]=*p++;
 if(!used)return 0;
 target[used]='\0';
 return 1;
}

static int extract_peer_target(const char *prompt,char *target,size_t target_size)
{
 const char *p,*to=NULL;
 if(!prompt||!target||target_size<2U)return 0;
 p=prompt;
 while(*p==' '||*p=='\t')++p;

 if(!strncmp(p,"ask ",4))return copy_target_token(p+4,target,target_size);
 if(!strncmp(p,"tell ",5))return copy_target_token(p+5,target,target_size);

 if(!strncmp(p,"say ",4)){
  const char *scan=p+4;
  while((scan=strstr(scan," to "))!=NULL){to=scan+4;scan+=4;}
  if(to)return copy_target_token(to,target,target_size);
 }
 return 0;
}

static int ensure_peer_prefix(const char *prompt,char *text,size_t text_size)
{
 char target[128],prefixed[sizeof(((sparkles_llm_response_t *)0)->text)];
 size_t target_len,text_len;
 if(!extract_peer_target(prompt,target,sizeof target))return 1;
 target_len=strlen(target);text_len=strlen(text);
 if(text_len>target_len&&!strncmp(text,target,target_len)&&text[target_len]==':')
  return 1;
 if(snprintf(prefixed,sizeof prefixed,"%s: %s",target,text)>=(int)sizeof prefixed)
  return 0;
 if(strlen(prefixed)+1U>text_size)return 0;
 memcpy(text,prefixed,strlen(prefixed)+1U);
 return 1;
}

static stnlabz_module_result_t send_peer_action(const char *text)
{
 if(!owner||!owner->send_message||!text||!*text)
  return STNLABZ_MODULE_ERR_INVALID_STATE;
 return owner->send_message(text)
  ? STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_INVALID_STATE;
}

static size_t write_cb(void *contents,size_t size,size_t nmemb,void *ctx)
{
 buffer_t *b=ctx; size_t bytes,need; char *next;
 if(!b||!contents||!size||!nmemb||nmemb>SIZE_MAX/size)return 0;
 bytes=size*nmemb;
 if(b->used>SPARKLES_OLLAMA_HTTP_MAX||bytes>SPARKLES_OLLAMA_HTTP_MAX-b->used)return 0;
 need=b->used+bytes+1U;
 if(need>b->capacity){
  size_t cap=b->capacity?b->capacity:4096U;
  while(cap<need){ if(cap>=SPARKLES_OLLAMA_HTTP_MAX/2U){cap=SPARKLES_OLLAMA_HTTP_MAX+1U;break;} cap*=2U; }
  if(cap>SPARKLES_OLLAMA_HTTP_MAX+1U)return 0;
  next=realloc(b->data,cap); if(!next)return 0; b->data=next;b->capacity=cap;
 }
 memcpy(b->data+b->used,contents,bytes);b->used+=bytes;b->data[b->used]=0;return bytes;
}

static stnlabz_module_result_t generate(const void *request,size_t request_size,
 void *response,size_t response_size,size_t *used,void *ctx)
{
 const sparkles_llm_request_t *in=request; sparkles_llm_response_t *out=response;
 CURL *curl=NULL; struct curl_slist *headers=NULL; CURLcode rc; long status=0;
 char body[4096]; buffer_t received={0};
 if(used)*used=0;
 if(!in||request_size!=sizeof(*in)||!out||response_size<sizeof(*out)||
    !memchr(in->prompt,0,sizeof in->prompt)||!in->prompt[0])
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(out,0,sizeof(*out));
 if(!(ctx==&schedule_extraction_mode ?
      sparkles_ollama_build_schedule_request(SPARKLES_OLLAMA_MODEL,in->prompt,
                                           body,sizeof body) :
      sparkles_ollama_build_request(SPARKLES_OLLAMA_MODEL,in->prompt,
                                   body,sizeof body)))
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 curl=curl_easy_init(); if(!curl)return STNLABZ_MODULE_ERR_START_FAILED;
 headers=curl_slist_append(headers,"Content-Type: application/json");
 curl_easy_setopt(curl,CURLOPT_URL,SPARKLES_OLLAMA_ENDPOINT);
 curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
 curl_easy_setopt(curl,CURLOPT_POSTFIELDS,body);
 curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)strlen(body));
 curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_cb);
 curl_easy_setopt(curl,CURLOPT_WRITEDATA,&received);
 curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,1000L);
 curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,120000L);
 rc=curl_easy_perform(curl);
 if(rc==CURLE_OK)(void)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
 curl_slist_free_all(headers);curl_easy_cleanup(curl);
 if(rc!=CURLE_OK||status!=200L||!received.data){
  fprintf(stderr,"[OLLAMA] request failed curl=%d http=%ld\n",(int)rc,status);
  free(received.data); return STNLABZ_MODULE_ERR_INVALID_STATE;
 }
 if(!sparkles_ollama_extract_response(received.data,out->text,sizeof out->text)){
  fprintf(stderr,"[OLLAMA] response parse failed bytes=%zu\n",received.used);
  free(received.data); return STNLABZ_MODULE_ERR_INVALID_STATE;
 }
 fprintf(stderr,"[OLLAMA] generation complete bytes=%zu\n",received.used);
 free(received.data);
 if(used)*used=sizeof(*out);
 return STNLABZ_MODULE_OK;
}

/* Ollama owns the default conversational controller; Core only dispatches. */
static stnlabz_module_result_t chat_controller(
 const stnlabz_module_command_t *command,
 stnlabz_module_command_reply_fn reply,
 void *reply_context, void *handler_context)
{
 sparkles_llm_request_t in;
 sparkles_llm_response_t out;
 char peer_target[128];
 size_t used=0U,len;
 stnlabz_module_result_t result;
 (void)handler_context;
 if(!command||!reply||
    !memchr(command->arguments,0,sizeof command->arguments))
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

 if(owner&&owner->invoke_service){
  sparkles_identity_question_t question={{0}};
  sparkles_identity_answer_t identity={{0}};
  size_t identity_used=0U;
  stnlabz_module_result_t identity_result;
  size_t question_length=strlen(command->arguments);
  if(question_length>=sizeof question.question)
   return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
  memcpy(question.question,command->arguments,question_length+1U);
  identity_result=owner->invoke_service(SPARKLES_IDENTITY_ANSWER_SERVICE,
      &question,sizeof question,&identity,sizeof identity,&identity_used);
  if(identity_result==STNLABZ_MODULE_OK){
   if(identity_used!=sizeof identity||
      !memchr(identity.answer,0,sizeof identity.answer)||!identity.answer[0])
    return STNLABZ_MODULE_ERR_INVALID_STATE;
   return reply(reply_context,identity.answer)
    ? STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_INVALID_STATE;
  }
  if(identity_result!=STNLABZ_MODULE_ERR_NOT_FOUND)
   return STNLABZ_MODULE_ERR_INVALID_STATE;
 }

 if(schedule_conversation(command,reply,reply_context))return STNLABZ_MODULE_OK;
 len=strlen(command->arguments);
 if(!len||len>=sizeof in.prompt)
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(&in,0,sizeof in);memset(&out,0,sizeof out);
 memset(peer_target,0,sizeof peer_target);
 memcpy(in.prompt,command->arguments,len+1U);
 result=generate(&in,sizeof in,&out,sizeof out,&used,NULL);
 if(result!=STNLABZ_MODULE_OK)return result;
 if(used!=sizeof out)return STNLABZ_MODULE_ERR_INVALID_STATE;
 if(!ensure_peer_prefix(in.prompt,out.text,sizeof out.text))
  return STNLABZ_MODULE_ERR_INVALID_STATE;
 if(extract_peer_target(in.prompt,peer_target,sizeof peer_target))
  return send_peer_action(out.text);
 return reply(reply_context,out.text)
  ? STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_INVALID_STATE;
}

static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
 unsigned int e=0,p=0;int ne=0,np=0;int ok;
 unsigned int se=0,sp=0;
 if(!q)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(q,0,sizeof(*q));
 ok=sparkles_ollama_run_selftests(&e,&p,&ne,&np);
 ok=sparkles_schedule_selftests(&se,&sp)&&ok;
 e+=se;p+=sp;
 q->tests_executed=e;q->tests_passed=p;q->tests_failed=e-p;
 q->negative_test_executed=ne;q->negative_test_passed=np;
 return ok?STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_QUALIFICATION;
}

static stnlabz_module_result_t start(const stnlabz_module_host_t *h)
{
 if(!h||!h->send_message||!h->invoke_service||
    !h->register_service||!h->unregister_service||
    !h->register_command||!h->unregister_command)
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)
  return STNLABZ_MODULE_ERR_START_FAILED;
 if(!h->register_service(SPARKLES_LLM_GENERATE_SERVICE,generate,NULL)){
  curl_global_cleanup();return STNLABZ_MODULE_ERR_START_FAILED;
 }
 service_registered=1;
 if(!h->register_command("chat",chat_controller,NULL)){
  (void)h->unregister_service(SPARKLES_LLM_GENERATE_SERVICE,NULL);
  service_registered=0;curl_global_cleanup();
  return STNLABZ_MODULE_ERR_START_FAILED;
 }
 command_registered=1;owner=h;
 fprintf(stderr,"[OLLAMA] service active model=%s endpoint=%s\n",
         SPARKLES_OLLAMA_MODEL,SPARKLES_OLLAMA_ENDPOINT);
 return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t stop(void)
{
 if(command_registered&&owner)
  (void)owner->unregister_command("chat",NULL);
 command_registered=0;
 if(service_registered&&owner)
  (void)owner->unregister_service(SPARKLES_LLM_GENERATE_SERVICE,NULL);
 service_registered=0;owner=NULL;curl_global_cleanup();
 return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor={
 "ollama","Sparkles Ollama LLM",OLLAMA_VERSION_MAJOR,OLLAMA_VERSION_MINOR,
 OLLAMA_VERSION_PATCH,STNLABZ_MODULE_API_MAJOR,STNLABZ_MODULE_API_MINOR,
 qualify,start,stop
};
const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void){return &descriptor;}
