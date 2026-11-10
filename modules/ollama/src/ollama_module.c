#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "stnlabz_abi/module.h"
#include "sparkles_llm.h"
#include "sparkles_identity.h"
#include "ollama_internal.h"

#define OLLAMA_VERSION_MAJOR 1U
#define OLLAMA_VERSION_MINOR 0U
#define OLLAMA_VERSION_PATCH 12U

typedef struct { char *data; size_t used; size_t capacity; } buffer_t;
static const stnlabz_module_host_t *owner;
static int service_registered;
static int command_registered;

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
 char body[4096]; buffer_t received={0}; (void)ctx;
 if(used)*used=0;
 if(!in||request_size!=sizeof(*in)||!out||response_size<sizeof(*out)||
    !memchr(in->prompt,0,sizeof in->prompt)||!in->prompt[0])
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(out,0,sizeof(*out));
 if(!sparkles_ollama_build_request(SPARKLES_OLLAMA_MODEL,in->prompt,body,sizeof body))
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
 curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,30000L);
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
 if(!q)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(q,0,sizeof(*q));
 ok=sparkles_ollama_run_selftests(&e,&p,&ne,&np);
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
