#define _POSIX_C_SOURCE 200809L
#include "sl_caps.h"
#include "sl_wire.h"
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAPS_RESPONSE_MAX (256U*1024U)
#define CAPS_SESSION_MAX 64U

typedef struct {
    char bytes[CAPS_RESPONSE_MAX+1U];
    size_t length;
} caps_body_t;

typedef struct {
    sparkles_sl_caps_worker_t *worker;
    char event_url[2048];
    char session_url[2048];
    unsigned char joined[CAPS_SESSION_MAX][16];
    unsigned int joined_count;
    long long ack;
    int has_ack;
} caps_state_t;

static int stopped(const caps_state_t *state)
{
    return atomic_load(state->worker->module_stop) ||
           atomic_load(&state->worker->session_stop);
}
static void caps_log(const char *message)
{
    fprintf(stderr,"[SL CAPS] %s\n",message);
}
static int valid_cap_url(const char *url)
{
    /* Seed and simulator cap URLs are trusted input, but never let a cap
     * redirect this transport to arbitrary HTTP, file, or local IPC. */
    return url && !strncmp(url,"https://",8U) &&
           strlen(url)>8U && strlen(url)<2048U;
}
static size_t collect(char *ptr,size_t size,size_t nmemb,void *context)
{
    caps_body_t *body=context;
    size_t n;
    if(nmemb && size>SIZE_MAX/nmemb)return 0;
    n=size*nmemb;
    if(n>CAPS_RESPONSE_MAX-body->length)return 0;
    memcpy(body->bytes+body->length,ptr,n);
    body->length+=n;
    body->bytes[body->length]=0;
    return n;
}
static int progress(void *context,curl_off_t dl_total,curl_off_t dl_now,
                    curl_off_t ul_total,curl_off_t ul_now)
{
    caps_state_t *state=context;
    (void)dl_total;(void)dl_now;(void)ul_total;(void)ul_now;
    return stopped(state)?1:0;
}
static long caps_post(caps_state_t *state,const char *url,const char *body,
                      caps_body_t *result,int event_poll)
{
    CURL *curl;
    struct curl_slist *headers=NULL;
    CURLcode rc;
    long status=0;
    if(!valid_cap_url(url)||!body||!result)return 0;
    memset(result,0,sizeof *result);
    curl=curl_easy_init();
    if(!curl)return 0;
    headers=curl_slist_append(NULL,"Content-Type: application/llsd+xml");
    if(!headers)goto done;
    if(curl_easy_setopt(curl,CURLOPT_URL,url)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_POSTFIELDS,body)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)strlen(body))!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,collect)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_WRITEDATA,result)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_TIMEOUT,event_poll?40L:20L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https")!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress)!=CURLE_OK ||
       curl_easy_setopt(curl,CURLOPT_XFERINFODATA,state)!=CURLE_OK)
        goto done;
    rc=curl_easy_perform(curl);
    if(rc==CURLE_OK) (void)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
done:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}
static xmlNodePtr first_element(xmlNodePtr parent)
{
    xmlNodePtr n;
    for(n=parent?parent->children:NULL;n;n=n->next)
        if(n->type==XML_ELEMENT_NODE)return n;
    return NULL;
}
static xmlNodePtr map_get(xmlNodePtr map,const char *key)
{
    xmlNodePtr node;
    if(!map || xmlStrcmp(map->name,BAD_CAST "map"))return NULL;
    for(node=first_element(map);node;node=node->next) {
        xmlChar *value;
        xmlNodePtr found;
        if(node->type!=XML_ELEMENT_NODE ||
           xmlStrcmp(node->name,BAD_CAST "key"))continue;
        value=xmlNodeGetContent(node);
        if(!value)return NULL;
        found=node->next;
        while(found&&found->type!=XML_ELEMENT_NODE)found=found->next;
        if(!strcmp((const char *)value,key)) {
            xmlFree(value);
            return found;
        }
        xmlFree(value);
    }
    return NULL;
}
static int field_text(xmlNodePtr node,char *dst,size_t cap)
{
    xmlChar *data;
    size_t n;
    if(!node||!dst||!cap)return 0;
    if(xmlStrcmp(node->name,BAD_CAST "string") &&
       xmlStrcmp(node->name,BAD_CAST "uuid") &&
       xmlStrcmp(node->name,BAD_CAST "integer"))return 0;
    data=xmlNodeGetContent(node);
    if(!data)return 0;
    n=strlen((const char *)data);
    if(!n||n>=cap) {xmlFree(data);return 0;}
    memcpy(dst,data,n+1U);
    xmlFree(data);
    return 1;
}
static int map_text(xmlNodePtr map,const char *key,char *dst,size_t cap)
{
    return field_text(map_get(map,key),dst,cap);
}
static xmlNodePtr llsd_map(const char *bytes,size_t length,xmlDocPtr *owned)
{
    xmlDocPtr doc;
    xmlNodePtr root,node;
    if(!bytes||!length||length>CAPS_RESPONSE_MAX)return NULL;
    doc=xmlReadMemory(bytes,(int)length,"sl_caps.xml",NULL,
                     XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    if(!doc)return NULL;
    root=xmlDocGetRootElement(doc);
    node=first_element(root);
    if(!root||xmlStrcmp(root->name,BAD_CAST "llsd")||!node||
       xmlStrcmp(node->name,BAD_CAST "map")) {
        xmlFreeDoc(doc);
        return NULL;
    }
    *owned=doc;
    return node;
}
static int joined(const caps_state_t *state,const unsigned char uuid[16])
{
    unsigned int i;
    for(i=0U;i<state->joined_count;++i)
        if(!memcmp(state->joined[i],uuid,16U))return 1;
    return 0;
}
static void note_joined(caps_state_t *state,const unsigned char uuid[16])
{
    if(joined(state,uuid))return;
    if(state->joined_count<CAPS_SESSION_MAX)
        memcpy(state->joined[state->joined_count++],uuid,16U);
    else caps_log("session tracking capacity reached");
}
static int accept_invitation(caps_state_t *state,const unsigned char uuid[16])
{
    char id[37],request[256];
    caps_body_t *response;
    int n;
    long status;
    if(joined(state,uuid))return 1;
    if(!valid_cap_url(state->session_url))return 0;
    sparkles_sl_uuid_format(uuid,id);
    n=snprintf(request,sizeof request,
       "<llsd><map><key>method</key><string>accept invitation</string>"
       "<key>session-id</key><uuid>%s</uuid></map></llsd>",id);
    if(n<0||(size_t)n>=sizeof request)return 0;
    response=calloc(1,sizeof *response);
    if(!response)return 0;
    status=caps_post(state,state->session_url,request,response,0);
    free(response);
    if(status>=200L&&status<300L) {
        note_joined(state,uuid);
        caps_log("session invitation accepted through ChatSessionRequest");
        return 1;
    }
    fprintf(stderr,"[SL CAPS] session invitation not accepted: HTTP %ld\n",status);
    return 0;
}
static void invitation_event(caps_state_t *state,xmlNodePtr body)
{
    char id[48]={0},from[48]={0},name[128]={0},message[SPARKLES_SL_IM_TEXT_MAX]={0};
    char type[24]={0};
    unsigned char session[16]={0},speaker[16]={0};
    xmlNodePtr instant,map;
    sparkles_sl_private_im_t im={0};
    if(!map_text(body,"session_id",id,sizeof id) ||
       !sparkles_sl_uuid_parse(id,session))return;
    instant=map_get(body,"instantmessage");
    map=map_get(instant,"message_params");
    if(!map)return; /* Text chat only; do not join voice invitations. */
    (void)map_text(map,"type",type,sizeof type);
    if(type[0]&&strcmp(type,"17"))return;
    /* ChatterBoxInvitation is itself an invitation even when older
     * simulators omit the outer type field. Acceptance is idempotent. */
    if(!accept_invitation(state,session))return;
    /* Only pass session messages if server has acknowledged membership. */
    if(!joined(state,session))return;
    if(!map_text(map,"from_id",from,sizeof from) ||
       !sparkles_sl_uuid_parse(from,speaker) ||
       !memcmp(speaker,state->worker->agent,16U) ||
       !memcmp(speaker,session,16U))return; /* Session is not a person. */
    if(!map_text(map,"from_name",name,sizeof name) ||
       !map_text(map,"message",message,sizeof message))return;
    memcpy(im.sender,speaker,16U);
    memcpy(im.conversation,session,16U);
    memcpy(im.name,name,strlen(name)+1U);
    memcpy(im.text,message,strlen(message)+1U);
    im.ambient=2U;
    if(state->worker->ingress &&
       !state->worker->ingress(&im,state->worker->context))
        caps_log("session event dropped: Core queue full");
}
static void session_start_event(caps_state_t *state,xmlNodePtr body)
{
    char id[48]={0};
    unsigned char session[16]={0};
    xmlNodePtr result=map_get(body,"success");
    xmlChar *status;
    int success=0;
    if(!result || xmlStrcmp(result->name,BAD_CAST "boolean"))return;
    status=xmlNodeGetContent(result);
    if(!status)return;
    success=!xmlStrcmp(status,BAD_CAST "1") ||
            !xmlStrcmp(status,BAD_CAST "true");
    xmlFree(status);
    if(!map_text(body,"session_id",id,sizeof id) ||
       !sparkles_sl_uuid_parse(id,session))return;
    if(memcmp(session,state->worker->group_chat,16U)!=0)return;
    if(success) {
        note_joined(state,session);
        caps_log("configured group chat session start confirmed by simulator");
    } else {
        caps_log("configured group chat session start rejected by simulator");
    }
}
static void session_event_reply(caps_state_t *state,xmlNodePtr body)
{
    char id[48]={0},event[64]={0},error[128]={0};
    unsigned char session[16];
    xmlNodePtr result=map_get(body,"success");
    xmlChar *value;
    int success;
    (void)state;
    if(!result || xmlStrcmp(result->name,BAD_CAST "boolean"))return;
    value=xmlNodeGetContent(result);
    if(!value)return;
    success=!xmlStrcmp(value,BAD_CAST "true") ||
            !xmlStrcmp(value,BAD_CAST "1");
    xmlFree(value);
    if(!map_text(body,"session_id",id,sizeof id) ||
       !sparkles_sl_uuid_parse(id,session))return;
    if(success) {
        caps_log("chat session event accepted by simulator");
        return;
    }
    (void)map_text(body,"event",event,sizeof event);
    (void)map_text(body,"error",error,sizeof error);
    /* These are simulator-controlled diagnostics; bound both strings and
     * print no session UUID, message body, credentials or cap URLs. */
    fprintf(stderr,"[SL CAPS] chat session event rejected: event=%.63s error=%.127s\n",
            event[0]?event:"unknown",error[0]?error:"unspecified");
}
static void session_closed_event(caps_state_t *state,xmlNodePtr body)
{
    char id[48]={0};
    unsigned char session[16]={0};
    unsigned int i;
    if(!map_text(body,"session_id",id,sizeof id) ||
       !sparkles_sl_uuid_parse(id,session))return;
    for(i=0U;i<state->joined_count;++i) {
        if(!memcmp(state->joined[i],session,16U)) {
            --state->joined_count;
            memcpy(state->joined[i],state->joined[state->joined_count],16U);
            caps_log("chat session closed by simulator");
            break;
        }
    }
}
static int consume_events(caps_state_t *state,caps_body_t *response)
{
    xmlDocPtr doc=NULL;
    xmlNodePtr map,events,node;
    char id[64];
    char *end;
    long long next_ack;
    map=llsd_map(response->bytes,response->length,&doc);
    if(!map)return 0;
    if(!map_text(map,"id",id,sizeof id))goto done;
    errno=0;
    next_ack=strtoll(id,&end,10);
    if(errno||*end||next_ack<0)goto done;
    events=map_get(map,"events");
    if(!events||xmlStrcmp(events->name,BAD_CAST "array"))goto done;
    /* Do not advance ack past an event we cannot parse structurally. */
    for(node=first_element(events);node;node=node->next) {
        char event_name[80];
        xmlNodePtr body;
        if(node->type!=XML_ELEMENT_NODE)continue;
        if(xmlStrcmp(node->name,BAD_CAST "map"))goto done;
        body=map_get(node,"body");
        if(!map_text(node,"message",event_name,sizeof event_name)||
           !body||xmlStrcmp(body->name,BAD_CAST "map"))goto done;
        if(!strcmp(event_name,"ChatterBoxInvitation"))
            invitation_event(state,body);
        else if(!strcmp(event_name,"ChatterBoxSessionStartReply"))
            session_start_event(state,body);
        else if(!strcmp(event_name,"ChatterBoxSessionEventReply"))
            session_event_reply(state,body);
        else if(!strcmp(event_name,"ForceCloseChatterBoxSession"))
            session_closed_event(state,body);
    }
    state->ack=next_ack;
    state->has_ack=1;
    xmlFreeDoc(doc);
    return 1;
done:
    xmlFreeDoc(doc);
    return 0;
}
static int negotiate(caps_state_t *state)
{
    caps_body_t *response;
    xmlDocPtr doc=NULL;
    xmlNodePtr root;
    long status;
    int okay=0;
    static const char request[]=
      "<llsd><array><string>EventQueueGet</string>"
      "<string>ChatSessionRequest</string></array></llsd>";
    response=calloc(1,sizeof *response);
    if(!response)return 0;
    status=caps_post(state,state->worker->seed_url,request,response,0);
    if(status!=200L)goto done;
    root=llsd_map(response->bytes,response->length,&doc);
    if(!root)goto done;
    okay=map_text(root,"EventQueueGet",state->event_url,
                  sizeof state->event_url) &&
         map_text(root,"ChatSessionRequest",state->session_url,
                  sizeof state->session_url) &&
         valid_cap_url(state->event_url) &&
         valid_cap_url(state->session_url);
done:
    if(doc)xmlFreeDoc(doc);
    free(response);
    return okay;
}
void *sparkles_sl_caps_run(void *argument)
{
    sparkles_sl_caps_worker_t *worker=argument;
    caps_state_t *state=calloc(1,sizeof *state);
    caps_body_t *response=calloc(1,sizeof *response);
    if(!state||!response)goto done;
    state->worker=worker;
    if(!valid_cap_url(worker->seed_url)||!negotiate(state)) {
        caps_log("seed capability negotiation unavailable");
        goto done;
    }
    caps_log("EventQueueGet and ChatSessionRequest capabilities obtained");
    while(!stopped(state)) {
        char request[256];
        long status;
        int n;
        if(state->has_ack)
            n=snprintf(request,sizeof request,
                "<llsd><map><key>ack</key><integer>%lld</integer>"
                "<key>done</key><boolean>false</boolean></map></llsd>",
                state->ack);
        else
            n=snprintf(request,sizeof request,
                "<llsd><map><key>ack</key><undef/>"
                "<key>done</key><boolean>false</boolean></map></llsd>");
        if(n<0||(size_t)n>=sizeof request)break;
        status=caps_post(state,state->event_url,request,response,1);
        if(stopped(state))break;
        if(status==200L) {
            if(!consume_events(state,response))
                caps_log("event queue returned unrecognized LLSD");
        } else if(status==404L) {
            caps_log("event queue invalidated: simulator session lost");
            break;
        } else if(status!=502L) {
            fprintf(stderr,"[SL CAPS] event poll returned HTTP %ld\n",status);
        }
        /* Avoid a hot loop if a simulator immediately rejects a poll. */
        if(status!=200L&&status!=502L) {
            struct timespec delay={1,0};
            nanosleep(&delay,NULL);
        }
    }
done:
    free(response);
    free(state);
    return NULL;
}
