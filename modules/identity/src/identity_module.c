#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stnlabz_abi/module.h"
#include "identity_internal.h"
#define IDENTITY_RECORD "/opt/rictus/modules/identity/identity.conf"
static identity_record_t record;
static const stnlabz_module_host_t *owner;
static int command_active,service_active;

static int get_order(unsigned index,char *out,size_t cap)
{
 size_t n;
 const char *text;
 if (!out||index<1U||index>3U) return 0;
 text=record.orders[index-1U];
 n=strlen(text);
 if (!n||n>=cap) return 0;
 memcpy(out,text,n+1U);
 return 1;
}
/* Render approved imperative orders as first-person statements.
 * The source record remains untouched; no LLM paraphrasing is involved. */
static int first_person_order(unsigned index,char *out,size_t cap)
{
 char text[256];
 size_t len;
 int n;
 if (!get_order(index,text,sizeof text)) return 0;
 len=strlen(text);
 if (len && text[len-1U]=='.') text[--len]=0;
 if (!len) return 0;
 text[0]=(char)tolower((unsigned char)text[0]);
 n=snprintf(out,cap,"I must %s",text);
 return n>0 && (size_t)n<cap;
}
static int answer(const char *topic,char *output,size_t cap)
{
 unsigned index=0;
 char order[300];
 int n;
 if (!topic||!output||cap<2U) return 0;
 if (!strcmp(topic,"orders")||!strcmp(topic,"general orders")) {
  char a[300],b[300],c[300];
  if (!first_person_order(1U,a,sizeof a)||!first_person_order(2U,b,sizeof b)||
      !first_person_order(3U,c,sizeof c)) return 0;
  n=snprintf(output,cap,"My three General Orders are: %s; %s; and %s.",a,b,c);
  return n>0&&(size_t)n<cap;
 }
 if (!strcmp(topic,"1")||!strcmp(topic,"first general order")) index=1U;
 else if (!strcmp(topic,"2")||!strcmp(topic,"second general order")) index=2U;
 else if (!strcmp(topic,"3")||!strcmp(topic,"third general order")) index=3U;
 if (index) {
  if (!first_person_order(index,order,sizeof order)) return 0;
  n=snprintf(output,cap,"My %s General Order is clear: %s.",
    index==1U?"first":index==2U?"second":"third",order);
  return n>0&&(size_t)n<cap;
 }
 return identity_reply(&record,topic,output,cap);
}
static stnlabz_module_result_t controller(const stnlabz_module_command_t *cmd,
 stnlabz_module_command_reply_fn reply,void *context,void *unused)
{
 char output[RICTUS_IDENTITY_ANSWER_MAX];
 (void)unused;
 if (!cmd||!reply||!memchr(cmd->arguments,0,sizeof cmd->arguments))
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 if (!answer(cmd->arguments,output,sizeof output))
  return STNLABZ_MODULE_ERR_INVALID_STATE;
 return reply(context,output)?STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_INVALID_STATE;
}
static stnlabz_module_result_t answer_service(const void *request,size_t request_size,
 void *response,size_t response_size,size_t *used,void *ctx)
{
 const rictus_identity_question_t *in=request;
 rictus_identity_answer_t *out=response;
 char topic[80];
 (void)ctx;
 if(used)*used=0;
 if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out||
    !memchr(in->question,0,sizeof in->question))
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 if (!identity_question_route(in->question,topic,sizeof topic))
  return STNLABZ_MODULE_ERR_NOT_FOUND;
 memset(out,0,sizeof *out);
 if(!answer(topic,out->answer,sizeof out->answer))
  return STNLABZ_MODULE_ERR_INVALID_STATE;
 if(used)*used=sizeof *out;
 return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
 unsigned e=0,p=0;int ne=0,np=0;
 if(!q)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 memset(q,0,sizeof *q);
 if(!identity_selftest(&e,&p,&ne,&np))return STNLABZ_MODULE_ERR_QUALIFICATION;
 q->tests_executed=e;q->tests_passed=p;q->tests_failed=e-p;
 q->negative_test_executed=ne;q->negative_test_passed=np;
 return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t start(const stnlabz_module_host_t *host)
{
 if(!host||!host->register_command||!host->unregister_command||
    !host->register_service||!host->unregister_service||!host->invoke_service)
  return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
 if(!identity_load(IDENTITY_RECORD,&record)) {
  fprintf(stderr,"[IDENTITY] approved record unavailable or invalid\n");
  return STNLABZ_MODULE_ERR_START_FAILED;
 }
 owner=host;
 if(!host->register_command("identity",controller,NULL))goto fail;
 command_active=1;
 if(!host->register_service(RICTUS_IDENTITY_ANSWER_SERVICE,answer_service,NULL))goto fail;
 service_active=1;
 fprintf(stderr,"[IDENTITY] mission and General Orders loaded from module data\n");
 return STNLABZ_MODULE_OK;
fail:
 if(command_active)(void)host->unregister_command("identity",NULL);
 command_active=0;owner=NULL;memset(&record,0,sizeof record);
 return STNLABZ_MODULE_ERR_START_FAILED;
}
static stnlabz_module_result_t stop(void)
{
 if(service_active&&owner&&!owner->unregister_service(RICTUS_IDENTITY_ANSWER_SERVICE,NULL))
  return STNLABZ_MODULE_ERR_STOP_FAILED;
 service_active=0;
 if(command_active&&owner&&!owner->unregister_command("identity",NULL))
  return STNLABZ_MODULE_ERR_STOP_FAILED;
 command_active=0;owner=NULL;memset(&record,0,sizeof record);
 return STNLABZ_MODULE_OK;
}
static const stnlabz_module_descriptor_t descriptor={
 "identity","Rictus Identity and Mission",1,1,3,
 STNLABZ_MODULE_API_MAJOR,STNLABZ_MODULE_API_MINOR,qualify,start,stop
};
const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void){return &descriptor;}
