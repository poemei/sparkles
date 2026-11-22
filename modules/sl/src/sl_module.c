#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include "sparkles_route.h"
#include "sparkles_memory.h"
#include "sparkles_sl_group.h"
#include "sparkles_sl_group_db.h"
#include "sparkles_show.h"
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include "stnlabz_abi/module.h"
#include "sl_wire.h"
#include "sl_grid.h"
#include "sl_hud.h"

/* Native C in-process Second Life grid transport. Core controls lifecycle.
 * A module start means only that the worker started; grid login and avatar
 * presence require distinct, simulator-observed protocol milestones.
 * No IRC service ownership and no claimed SL identity from local IPC. */
/* Advance the module descriptor for hotload admission. The prior
 * 0.3.24 descriptor predates STN-LABZ revision rollover policy;
 * use the next higher, policy-compliant release boundary. */
#define SL_MAJOR 0U
#define SL_MINOR 4U
#define SL_PATCH 3U

#define SL_INBOUND_CAP 128U
#define SL_OUTBOUND_CAP 8U

static pthread_t worker, dispatcher, show_timer;
static atomic_int stopping=ATOMIC_VAR_INIT(0);
static pthread_mutex_t qlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static sparkles_sl_private_im_t inbound[SL_INBOUND_CAP];
static sparkles_sl_im_reply_t outbound[SL_OUTBOUND_CAP];
static size_t in_head, in_tail, in_count, out_head, out_tail, out_count;
static uint64_t epoch;
static int worker_started, dispatcher_started, show_timer_started;
static atomic_int sl_present=ATOMIC_VAR_INIT(0);
static const stnlabz_module_host_t *module_host;
static char active_private[128];
static int group_service_registered;
static int notice_service_registered;

/* A simulator-authenticated private IM is dispatched synchronously by
 * this worker. IRC and group speakers cannot impersonate this context. */
/* Only the actual SL dispatcher handling a simulator-authenticated private
 * IM can call this service. MariaDB independently resolves the destination
 * from a linked active business owned by the sender. No sl.conf groups,
 * fixed slots, or inferred group UUIDs. */
static stnlabz_module_result_t group_start_service(
    const void *request,size_t request_size,void *response,
    size_t response_size,size_t *response_used,void *unused)
{
    const sparkles_sl_group_start_request_t *in=request;
    sparkles_sl_group_start_reply_t *out=response;
    sparkles_sl_group_resolve_request_t lookup={0};
    sparkles_sl_group_resolve_reply_t found={0};
    sparkles_sl_im_reply_t action={0};
    size_t found_used=0U;
    stnlabz_module_result_t status;
    uint64_t generation;
    int permitted,accepted=0;
    (void)unused;
    if(response_used)*response_used=0U;
    if(!in||!out||request_size!=sizeof *in||
       response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account) ||
       !memchr(in->group,0,sizeof in->group) ||
       !memchr(in->opening,0,sizeof in->opening) ||
       !in->opening[0] || !in->group[0])
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    pthread_mutex_lock(&qlock);
    permitted=!atomic_load(&stopping) && dispatcher_started &&
       pthread_equal(pthread_self(),dispatcher) &&
       active_private[0] && !strcmp(active_private,in->account);
    generation=epoch;
    pthread_mutex_unlock(&qlock);
    if(!permitted || !module_host || !module_host->invoke_service)
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    memcpy(lookup.account,in->account,strlen(in->account)+1U);
    memcpy(lookup.group_name,in->group,strlen(in->group)+1U);
    status=module_host->invoke_service(SPARKLES_SL_GROUP_RESOLVE_SERVICE,
                  &lookup,sizeof lookup,&found,sizeof found,&found_used);
    if(status!=STNLABZ_MODULE_OK ||
       found_used!=sizeof found || !found.matched ||
       !sparkles_sl_uuid_parse(found.group_uuid,action.conversation))
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    memcpy(action.recipient,action.conversation,16U);
    memcpy(action.text,in->opening,sizeof action.text);
    action.session_chat=1U;
    action.start_group=1U;
    pthread_mutex_lock(&qlock);
    if(!atomic_load(&stopping) && generation==epoch &&
       dispatcher_started && pthread_equal(pthread_self(),dispatcher) &&
       active_private[0] && !strcmp(active_private,in->account) &&
       out_count<SL_OUTBOUND_CAP) {
        outbound[out_tail]=action;
        out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
        ++out_count;
        accepted=1;
    }
    pthread_mutex_unlock(&qlock);
    if(!accepted)return STNLABZ_MODULE_ERR_INVALID_STATE;
    out->queued=1;
    if(response_used)*response_used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Authenticated private-IM-only, on-demand official group notice.
 * This service queues a notice; the simulator may still deny publication. */
static stnlabz_module_result_t notice_send_service(
    const void *request,size_t request_size,void *response,
    size_t response_size,size_t *response_used,void *unused)
{
    const sparkles_sl_notice_send_request_t *in=request;
    sparkles_sl_notice_send_reply_t *out=response;
    sparkles_sl_group_resolve_request_t lookup={0};
    sparkles_sl_group_resolve_reply_t found={0};
    sparkles_sl_im_reply_t action={0};
    size_t used=0U,i,subject_len,body_len;
    stnlabz_module_result_t rc;
    uint64_t generation;
    int permitted,queued=0;
    (void)unused;
    if(response_used)*response_used=0U;
    if(!in||!out||request_size!=sizeof *in||response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account)||
       !memchr(in->group,0,sizeof in->group)||
       !memchr(in->subject,0,sizeof in->subject)||
       !memchr(in->body,0,sizeof in->body)||
       !in->account[0]||!in->group[0]||!in->subject[0]||!in->body[0])
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    subject_len=strlen(in->subject);
    body_len=strlen(in->body);
    if(subject_len+body_len+1U>510U)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    for(i=0U;i<subject_len;++i)
        if(in->subject[i]=='|' || (unsigned char)in->subject[i]<0x20U)
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    for(i=0U;i<body_len;++i)
        if(in->body[i]=='\r' ||
           ((unsigned char)in->body[i]<0x20U && in->body[i]!='\n'))
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&qlock);
    permitted=!atomic_load(&stopping)&&dispatcher_started &&
        pthread_equal(pthread_self(),dispatcher) && active_private[0] &&
        !strcmp(active_private,in->account);
    generation=epoch;
    pthread_mutex_unlock(&qlock);
    if(!permitted||!module_host||!module_host->invoke_service) {
        fprintf(stderr,"[SL HUD] notice authorization denied before group lookup: "
                       "dispatcher=%d host=%d group=%s account=%s\n",
                permitted,module_host&&module_host->invoke_service?1:0,
                in->group,in->account);
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    }
    memcpy(lookup.account,in->account,strlen(in->account)+1U);
    memcpy(lookup.group_name,in->group,strlen(in->group)+1U);
    lookup.notice=1U;
    rc=module_host->invoke_service(SPARKLES_SL_GROUP_RESOLVE_SERVICE,
          &lookup,sizeof lookup,&found,sizeof found,&used);
    if(rc!=STNLABZ_MODULE_OK||used!=sizeof found||!found.matched||
       !sparkles_sl_uuid_parse(found.group_uuid,action.recipient)) {
        fprintf(stderr,"[SL HUD] group lookup failed: group=%s account=%s "
                       "service_rc=%d response_used=%zu expected=%zu "
                       "matched=%u uuid=%s\n",
                in->group,in->account,(int)rc,used,sizeof found,
                found.matched,found.group_uuid);
        return rc==STNLABZ_MODULE_ERR_INVALID_STATE?
               STNLABZ_MODULE_ERR_INVALID_STATE:
               STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    }
    memcpy(action.conversation,action.recipient,16U);
    memcpy(action.subject,in->subject,subject_len+1U);
    memcpy(action.text,in->body,body_len+1U);
    action.notice=1U;
    pthread_mutex_lock(&qlock);
    if(!atomic_load(&stopping)&&generation==epoch&&
       dispatcher_started&&pthread_equal(pthread_self(),dispatcher)&&
       active_private[0]&&!strcmp(active_private,in->account)&&
       out_count<SL_OUTBOUND_CAP) {
        outbound[out_tail]=action;
        out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
        ++out_count;
        queued=1;
    }
    pthread_mutex_unlock(&qlock);
    if(!queued) {
        fprintf(stderr,"[SL HUD] outgoing notice queue refused: "
                       "group=%s account=%s\n",in->group,in->account);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    out->queued=1U;
    if(response_used)*response_used=sizeof *out;
    return STNLABZ_MODULE_OK;
}


/* AgentData.AgentID in simulator-delivered object IM is the owner UUID.
 * MessageBlock.ID identifies the sending object. The avatar UUID is not
 * supplied by untrusted command text. Group permission is resolved through
 * the existing MariaDB ownership/notice service for each requested group. */
static int hud_simulator_identity(const sparkles_sl_private_im_t *im,
                                  char operator_uuid[37])
{
    unsigned char zero[16]={0};
    if(!im||im->ambient!=3U||!memcmp(im->sender,zero,16U)||
       !memcmp(im->conversation,zero,16U))return 0;
    sparkles_sl_uuid_format(im->sender,operator_uuid);
    return 1;
}

/* Public touch signage uses the existing STNLINK1 object request protocol.
 * The simulator authenticates the sending object's owner; LSL supplies
 * the visitor UUID, which cannot independently be proven by object IM.
 * The fixed, operator-directed welcome is sent by Sparkles as private IM. */

/* Called by the capability worker only after an explicit membership result.
 * No result, partial response, or currently-known member causes an invite. */
static void enqueue_membership_result(
    const sparkles_sl_membership_request_t *request,int result,void *unused)
{
    sparkles_sl_im_reply_t action={0};
    int queued=0;
    (void)unused;
    if(!request||result!=0)return;
    memcpy(action.recipient,request->group,16U);
    memcpy(action.visitor,request->visitor,16U);
    action.invite_group=1U;
    pthread_mutex_lock(&qlock);
    if(!atomic_load(&stopping)&&out_count<SL_OUTBOUND_CAP) {
        outbound[out_tail]=action;
        out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
        ++out_count;
        queued=1;
    }
    pthread_mutex_unlock(&qlock);
    fprintf(stderr,"[SL LINKS] confirmed nonmember group invite %s; "
            "simulator acceptance unverified\n",
            queued?"queued":"not queued");
}
static void dispatch_group_links_im(const sparkles_sl_private_im_t *im)
{
    static unsigned char recent_visitors[64][16]={{0}};
    static time_t recent_at[64]={0};
    static size_t next_visitor=0U;
    sparkles_sl_group_links_request_t request={0};
    sparkles_sl_im_reply_t action={0};
    char owner_uuid[37]={0};
    time_t now;
    size_t i;
    int n,queued=0;
    if(!im||!hud_simulator_identity(im,owner_uuid)||
       !sparkles_sl_group_links_parse(im->text,&request)) {
        fprintf(stderr,"[SL WELCOME] malformed object request rejected\n");
        return;
    }
    now=time(NULL);
    for(i=0U;i<64U;++i)
        if(!memcmp(recent_visitors[i],request.visitor,16U) &&
           now!=((time_t)-1) && recent_at[i]!=0 &&
           now>=recent_at[i] && now-recent_at[i]<30) {
            fprintf(stderr,"[SL WELCOME] duplicate visitor request suppressed\n");
            return;
        }
    n=snprintf(action.text,sizeof action.text,
        "Hello! 💜\n"
        "Welcome to STN-Labz!\n"
        "I'm Sparkles, your Executive AI Assistant.\n"
        "I'm here to assist with Second Life business, communications, organization, and general questions.\n"
        "Would you like some assistance today?\n"
        "Simply reply to this IM and tell me what you need.\n"
        "~~ Sparkles AI | STN-Labz ~~");
    if(n<=0||(size_t)n>=sizeof action.text)return;
    memcpy(action.recipient,request.visitor,16U);
    memcpy(action.conversation,request.visitor,16U);
    pthread_mutex_lock(&qlock);
    if(!atomic_load(&stopping)&&dispatcher_started&&
       pthread_equal(pthread_self(),dispatcher)&&out_count<SL_OUTBOUND_CAP) {
        outbound[out_tail]=action;
        out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
        ++out_count;
        queued=1;
        memcpy(recent_visitors[next_visitor],request.visitor,16U);
        recent_at[next_visitor]=now;
        next_visitor=(next_visitor+1U)%64U;
    }
    pthread_mutex_unlock(&qlock);
    fprintf(stderr,"[SL WELCOME] visitor welcome %s; IM delivery unverified\n",
            queued?"queued":"not queued");
}
static void dispatch_hud_im(const sparkles_sl_private_im_t *im)
{
    static char recent_requests[32][SPARKLES_SL_HUD_REQUEST_MAX]={{0}};
    static size_t next_request=0U;
    size_t i;
    sparkles_sl_hud_request_t request={0};
    sparkles_sl_notice_send_reply_t response={0};
    char operator_uuid[37]={0};
    size_t used=0U;
    stnlabz_module_result_t rc;
    if(!im||im->ambient!=3U||!module_host||
       !hud_simulator_identity(im,operator_uuid)) {
        fprintf(stderr,"[SL HUD] object command rejected: missing simulator identity\n");
        return;
    }
    if(!sparkles_sl_hud_parse(im->text,operator_uuid,&request)) {
        fprintf(stderr,"[SL HUD] malformed command rejected\n");
        return;
    }
    for(i=0U;i<32U;++i) {
        if(!strcmp(request.request_id,recent_requests[i])) {
            fprintf(stderr,"[SL HUD] recent duplicate command rejected\n");
            return;
        }
    }
    /* Only this isolated object-origin branch can enter this path.
     * Actual notice permission is enforced by the MariaDB resolver in
     * notice_send_service; neither HUD-selected groups nor UUID text
     * can override it. Recent replay protection is in-memory. */
    memcpy(recent_requests[next_request],request.request_id,
           sizeof request.request_id);
    next_request=(next_request+1U)%32U;
    pthread_mutex_lock(&qlock);
    memcpy(active_private,request.notice.account,
           strlen(request.notice.account)+1U);
    pthread_mutex_unlock(&qlock);
    rc=notice_send_service(&request.notice,sizeof request.notice,
                           &response,sizeof response,&used,NULL);
    pthread_mutex_lock(&qlock);
    memset(active_private,0,sizeof active_private);
    pthread_mutex_unlock(&qlock);
    fprintf(stderr,
        "[SL HUD] simulator-attributed object request %s: %s "
        "(service_rc=%d response_used=%zu queued=%u); publication unverified\n",
        request.request_id,
        rc==STNLABZ_MODULE_OK&&used==sizeof response&&response.queued?
        "notice queued":"notice rejected/not queued",
        (int)rc,used,response.queued);
}
/* Session chat is shared. Only an explicit address to Sparkles triggers
 * inference; all other session lines are recorded without replies. */
static int session_addressed(const char *text)
{
    const char *p;
    if(!text)return 0;
    for(p=text;*p;++p) {
        if((p==text || !((* (p-1)>='A' && *(p-1)<='Z') ||
                         (*(p-1)>='a' && *(p-1)<='z'))) &&
           !strncasecmp(p,"sparkles",8U) &&
           (p[8U]==0 || !((p[8U]>='A'&&p[8U]<='Z') ||
                          (p[8U]>='a'&&p[8U]<='z'))))
            return 1;
    }
    return 0;
}

static int enqueue_private(const sparkles_sl_private_im_t *im, void *unused)
{
    int accepted=0;
    (void)unused;
    if (!im) return 0;
    pthread_mutex_lock(&qlock);
    if (!atomic_load(&stopping) && in_count<SL_INBOUND_CAP) {
        inbound[in_tail]=*im;
        in_tail=(in_tail+1U)%SL_INBOUND_CAP;
        ++in_count;
        pthread_cond_signal(&ready);
        accepted=1;
    }
    pthread_mutex_unlock(&qlock);
    if (!accepted) fprintf(stderr,"[SL] inbound IM queue full; message not dispatched\n");
    return accepted;
}
static int dequeue_reply(sparkles_sl_im_reply_t *reply, void *unused)
{
    int available=0;
    (void)unused;
    if (!reply) return 0;
    pthread_mutex_lock(&qlock);
    if (!atomic_load(&stopping) && out_count) {
        *reply=outbound[out_head];
        memset(&outbound[out_head],0,sizeof outbound[out_head]);
        out_head=(out_head+1U)%SL_OUTBOUND_CAP;
        --out_count;
        available=1;
    }
    pthread_mutex_unlock(&qlock);
    return available;
}
/* A reconnect invalidates pending exchanges; do not route stale IM
 * responses into a later SL session. Core calls already in flight
 * are discarded using the session epoch. */
static void presence_update(int online,void *unused)
{
    (void)unused;
    atomic_store(&sl_present,online?1:0);
}
static void reset_queues(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&qlock);
    ++epoch;
    memset(inbound,0,sizeof inbound);
    memset(outbound,0,sizeof outbound);
    memset(active_private,0,sizeof active_private);
    in_head=in_tail=in_count=0U;
    out_head=out_tail=out_count=0U;
    pthread_mutex_unlock(&qlock);
}
static void *dispatch_worker(void *unused)
{
    (void)unused;
    for (;;) {
        sparkles_sl_private_im_t im;
        sparkles_sl_im_reply_t reply={0};
        sparkles_route_request_t request={0};
        sparkles_route_reply_t response={0};
        size_t used=0U;
        uint64_t generation;
        stnlabz_module_result_t result;
        pthread_mutex_lock(&qlock);
        while (!atomic_load(&stopping) && !in_count)
            pthread_cond_wait(&ready,&qlock);
        if (atomic_load(&stopping)) {
            pthread_mutex_unlock(&qlock);
            return NULL;
        }
        im=inbound[in_head];
        memset(&inbound[in_head],0,sizeof inbound[in_head]);
        in_head=(in_head+1U)%SL_INBOUND_CAP;
        --in_count;
        generation=epoch;
        pthread_mutex_unlock(&qlock);
        if (!module_host || !module_host->invoke_service) continue;
        if(im.ambient==3U) {
            if(!strncmp(im.text,"STNLINK1|",9U))
                dispatch_group_links_im(&im);
            else
                dispatch_hud_im(&im);
            continue;
        }
        {
            char id[37];
            sparkles_sl_uuid_format(im.sender,id);
            if (snprintf(request.account,sizeof request.account,"sl:%s",id)
                >= (int)sizeof request.account) continue;
        }
        memcpy(request.sender,im.name,sizeof request.sender);
        if (!request.sender[0]) {
            memcpy(request.sender,request.account,
                   strlen(request.account)+1U);
        }
        memcpy(request.text,im.text,sizeof request.text);
        if(im.ambient==2U) {
            char session_id[37];
            sparkles_sl_uuid_format(im.conversation,session_id);
            if(snprintf(request.account,sizeof request.account,
                        "slg-%s",session_id)>=(int)sizeof request.account)
                continue;
        }
        /* Both IM and local chat reach durable context before any optional
         * conversational response. Identity answers, scheduling paths, and
         * brief messages must not bypass the observation write. */
        {
            sparkles_memory_exchange_request_t observation={0};
            sparkles_memory_exchange_reply_t receipt={0};
            size_t memory_used=0U;
            stnlabz_module_result_t memory_result;
            size_t text_length=strlen(im.text);
            if(im.ambient==2U) {
                char speaker_id[37];
                sparkles_sl_uuid_format(im.sender,speaker_id);
                if(snprintf(observation.account,sizeof observation.account,
                            "sl:%s",speaker_id)>=
                   (int)sizeof observation.account)continue;
            } else {
                memcpy(observation.account,request.account,
                       strlen(request.account)+1U);
            }
            if(text_length>SPARKLES_MEMORY_TEXT_MAX) {
                fprintf(stderr,"[SL] observation exceeds context limit; not saved\n");
            } else {
                memcpy(observation.utterance,im.text,text_length+1U);
                observation.record=1U;
                observation.ambient=im.ambient;
                memory_result=module_host->invoke_service(
                    SPARKLES_MEMORY_EXCHANGE_SERVICE,
                    &observation,sizeof observation,
                    &receipt,sizeof receipt,&memory_used);
                if(memory_result!=STNLABZ_MODULE_OK ||
                   memory_used!=sizeof receipt || !receipt.saved)
                    fprintf(stderr,
                        "[SL] context observation NOT saved: result=%d used=%zu saved=%u channel=%s\n",
                        (int)memory_result,memory_used,receipt.saved,
                        im.ambient==2U?"session":im.ambient?"nearby":"private");
                else
                    fprintf(stderr,"[SL] context observation saved: channel=%s\n",
                            im.ambient==2U?"session":im.ambient?"nearby":"private");
            }
        }
        /* Nearby talk is public conversation, not an authorized IM.
         * A separate Core account namespace ensures no private calendar,
         * contact, or IM history can be used as public authority. It is
         * still attributed to the simulator-verified avatar UUID. */
        if(im.ambient==1U) {
            char id[37];
            sparkles_sl_uuid_format(im.sender,id);
            if(snprintf(request.account,sizeof request.account,
                        "slnear:%s",id)>=(int)sizeof request.account)
                continue;
        }
        /* Public channels never execute slash commands or private tools.
         * Session chat retains its existing explicit-address boundary. */
        /* Observe ordinary nearby speech, but do not send unsolicited
         * replies to gestures or overheard conversations. Direct address
         * is required before consuming an Ollama inference slot. */
        if((im.ambient==1U &&
            (im.text[0]=='/' || !sparkles_sl_nearby_addressed(im.text))) ||
           (im.ambient==2U && (im.text[0]=='/' ||
                                !session_addressed(im.text))))continue;
        if(im.ambient==0U) {
            pthread_mutex_lock(&qlock);
            memcpy(active_private,request.account,sizeof active_private);
            pthread_mutex_unlock(&qlock);
        }
        /* Only a simulator-verified private IM can answer a show
         * check-in. Explicit phrases don't hijack normal confirmations. */
        if(im.ambient==0U &&
           (!strcasecmp(im.text,"show yes") ||
            !strcasecmp(im.text,"show cancel"))) {
            sparkles_show_response_request_t answer={0};
            sparkles_show_response_reply_t acknowledged={0};
            size_t answer_used=0U;
            stnlabz_module_result_t answer_rc;
            memcpy(answer.account,request.account,strlen(request.account)+1U);
            answer.answer=!strcasecmp(im.text,"show yes")?1U:2U;
            answer_rc=module_host->invoke_service(SPARKLES_SHOW_RESPONSE_SERVICE,
                &answer,sizeof answer,&acknowledged,sizeof acknowledged,&answer_used);
            if(answer_rc==STNLABZ_MODULE_OK &&
               answer_used==sizeof acknowledged && acknowledged.updated) {
                const char *confirmation=answer.answer==1U?
                   "Show confirmation recorded. Scheduled group notices remain enabled.":
                   "The show's automatic group notices are cancelled. "
                   "Your calendar appointment is unchanged.";
                memcpy(response.text,confirmation,strlen(confirmation)+1U);
            } else {
                const char *missing="No single outstanding show check-in "
                    "was found. No show record was changed.";
                memcpy(response.text,missing,strlen(missing)+1U);
            }
            response.replied=1U;
            used=sizeof response;
            result=STNLABZ_MODULE_OK;
        } else {
            result=module_host->invoke_service(
                SPARKLES_ROUTER_DISPATCH_SERVICE,
                &request,sizeof request,&response,sizeof response,&used);
        }
        pthread_mutex_lock(&qlock);
        memset(active_private,0,sizeof active_private);
        pthread_mutex_unlock(&qlock);
        if (result!=STNLABZ_MODULE_OK || used!=sizeof response ||
            !response.replied ||
            !memchr(response.text,0,sizeof response.text) ||
            !response.text[0]) {
            if(im.ambient!=1U)
                fprintf(stderr,"[SL] %s Core dispatch returned no reply\n",
                        im.ambient==2U?"session chat":"private IM");
            continue;
        }
        if(im.ambient==2U)
            memcpy(reply.recipient,im.conversation,16U);
        else
            memcpy(reply.recipient,im.sender,16U);
        memcpy(reply.conversation,im.conversation,16U);
        reply.session_chat=im.ambient==2U;
        reply.nearby=im.ambient==1U;
        memcpy(reply.text,response.text,sizeof reply.text);
        pthread_mutex_lock(&qlock);
        if (!atomic_load(&stopping) && generation==epoch &&
            out_count<SL_OUTBOUND_CAP) {
            outbound[out_tail]=reply;
            out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
            ++out_count;
        } else {
            fprintf(stderr,"[SL] %s response discarded (disconnect or full queue)\n",
                    im.ambient==2U?"session chat":
                    im.ambient==1U?"nearby chat":"private IM");
        }
        pthread_mutex_unlock(&qlock);
    }
}
/* Polling is isolated from the socket and inference dispatch threads.
 * Claiming is an at-most-once DB reservation, NOT a delivery receipt.
 * Once claimed, a failed/missed send requires operator reconciliation. */
static void *show_timer_worker(void *unused)
{
    (void)unused;
    while(!atomic_load(&stopping)) {
        sparkles_show_claim_reply_t result={0};
        sparkles_show_attempt_request_t receipt={0};
        sparkles_sl_im_reply_t action={0};
        size_t used=0U,ignored=0U;
        stnlabz_module_result_t rc;
        uint64_t generation;
        int capacity_ok=0;
        (void)poll(NULL,0,3000);
        if(atomic_load(&stopping) || !atomic_load(&sl_present) ||
           !module_host || !module_host->invoke_service)continue;
        pthread_mutex_lock(&qlock);
        capacity_ok=out_count+2U<SL_OUTBOUND_CAP;
        generation=epoch;
        pthread_mutex_unlock(&qlock);
        if(!capacity_ok)continue;
        rc=module_host->invoke_service(SPARKLES_SHOW_CLAIM_SERVICE,
                        NULL,0U,&result,sizeof result,&used);
        if(rc!=STNLABZ_MODULE_OK || used!=sizeof result) {
            /* Legacy databases without show_events simply remain inactive. */
            continue;
        }
        if(!result.available)continue;
        receipt.attempt_id=result.action.attempt_id;
        if((result.action.kind!=SPARKLES_SHOW_CONFIRMATION &&
            result.action.kind!=SPARKLES_SHOW_NOTICE) ||
           !memchr(result.action.message,0,sizeof result.action.message) ||
           !memchr(result.action.subject,0,sizeof result.action.subject) ||
           !sparkles_sl_uuid_parse(result.action.recipient_uuid,
                                   action.recipient)) {
            fprintf(stderr,"[SL] invalid claimed show task; marked failed\n");
            goto persist_attempt;
        }
        memcpy(action.text,result.action.message,
               strlen(result.action.message)+1U);
        if(result.action.kind==SPARKLES_SHOW_NOTICE) {
            action.notice=1U;
            memcpy(action.subject,result.action.subject,
                   strlen(result.action.subject)+1U);
            memcpy(action.conversation,action.recipient,16U);
        } else {
            memcpy(action.conversation,action.recipient,16U);
        }
        pthread_mutex_lock(&qlock);
        if(!atomic_load(&stopping) && atomic_load(&sl_present) &&
           epoch==generation && out_count<SL_OUTBOUND_CAP) {
            outbound[out_tail]=action;
            out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
            ++out_count;
            receipt.queued=1U;
        }
        pthread_mutex_unlock(&qlock);
persist_attempt:
        rc=module_host->invoke_service(SPARKLES_SHOW_ATTEMPT_SERVICE,
                         &receipt,sizeof receipt,NULL,0U,&ignored);
        if(rc!=STNLABZ_MODULE_OK)
            fprintf(stderr,"[SL] claimed show attempt status persistence failed\n");
        fprintf(stderr,"[SL] show task %s; simulator delivery is NOT confirmed\n",
                receipt.queued?"queued":"not queued");
    }
    return NULL;
}
/* The grid supplied the group UUID inside an authenticated simulator
 * membership invitation. Authorization is entirely registry-driven;
 * the bot's UUID is not a business owner UUID and must never be used as
 * an asserted human identity for an arbitrary message. */
static int authorize_group_membership_invite(const unsigned char group[16],
                                              void *unused)
{
    sparkles_sl_group_invite_auth_request_t request={0};
    sparkles_sl_group_invite_auth_reply_t result={0};
    stnlabz_module_result_t rc;
    size_t used=0U;
    (void)unused;
    if(!group||atomic_load(&stopping)||!atomic_load(&sl_present)||
       !module_host||!module_host->invoke_service)return 0;
    sparkles_sl_uuid_format(group,request.group_uuid);
    if(!request.group_uuid[0])return 0;
    rc=module_host->invoke_service(SPARKLES_SL_GROUP_INVITE_AUTH_SERVICE,
         &request,sizeof request,&result,sizeof result,&used);
    /* Explain precisely which registry gate failed. The group UUID is
     * a public SL identifier; do not log credentials or invite message. */
    if(rc!=STNLABZ_MODULE_OK || used!=sizeof result || result.allowed!=1U)
        fprintf(stderr,
            "[SL] group invite registry check denied: group=%s service_rc=%d response_bytes=%zu allowed=%u\n",
            request.group_uuid,(int)rc,used,result.allowed);
    return rc==STNLABZ_MODULE_OK&&used==sizeof result&&result.allowed==1U;
}
static void *grid_worker(void *unused)
{
    (void)unused;
    sparkles_sl_grid_run(&stopping,enqueue_private,
                         dequeue_reply,reset_queues,presence_update,
                         authorize_group_membership_invite,
                         enqueue_membership_result,NULL);
    return NULL;
}
static stnlabz_module_result_t sl_qualify(
    stnlabz_module_qualification_result_t *result)
{
    unsigned char uuid[16]={0}, agent[16]={0}, session_id[16]={0};
    unsigned char packet[SPARKLES_SL_PACKET_MAX]={0};
    unsigned char decoded[SPARKLES_SL_PACKET_MAX]={0};
    char formatted[37]={0};
    sparkles_sl_session_t session={{0},{0},0x12345678U};
    sparkles_sl_packet_view_t view;
    size_t used=0U;
    unsigned int i;
    unsigned int count=0U, passed=0U;
    int ok;

#define CHECK(test) do { ++count; if ((test)) ++passed; } while (0)

    if (!result) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(result,0,sizeof *result);

    CHECK(sparkles_sl_uuid_parse(
        "12345678-1234-1234-1234-123456789abc",uuid));
    sparkles_sl_uuid_format(uuid,formatted);
    CHECK(strcmp(formatted,"12345678-1234-1234-1234-123456789abc")==0);
    CHECK(!sparkles_sl_uuid_parse("invalid",uuid));
    CHECK(!sparkles_sl_uuid_parse(
        "12345678-1234-1234-1234-123456789abz",uuid));
    CHECK(!sparkles_sl_uuid_parse(
        "123456781234-1234-1234-123456789abc",uuid));
    CHECK(!sparkles_sl_uuid_parse(NULL,uuid));
    CHECK(!sparkles_sl_uuid_parse(
        "12345678-1234-1234-1234-123456789abc",NULL));

    for (i=0U;i<16U;++i) {
        agent[i]=(unsigned char)(i+1U);
        session_id[i]=(unsigned char)(i+17U);
    }
    memcpy(session.agent,agent,16U);
    memcpy(session.session,session_id,16U);

    ok=sparkles_sl_use_circuit(&session,1U,packet,sizeof packet,&used);
    CHECK(ok && used==46U);
    CHECK(ok && packet[0]==SPARKLES_SL_FLAG_RELIABLE &&
          packet[6]==0xffU && packet[7]==0xffU &&
          packet[8]==0U && packet[9]==3U);
    CHECK(ok && packet[10]==0x78U && packet[11]==0x56U &&
          packet[12]==0x34U && packet[13]==0x12U);
    CHECK(ok && !memcmp(packet+14U,session.session,16U) &&
          !memcmp(packet+30U,session.agent,16U));
    CHECK(ok && sparkles_sl_packet_view(packet,used,decoded,
          sizeof decoded,&view) && view.message_frequency==2U &&
          view.message_id==3U && view.sequence==1U);
    CHECK(!sparkles_sl_use_circuit(&session,0U,packet,sizeof packet,&used));
    CHECK(!sparkles_sl_use_circuit(&session,1U,packet,45U,&used));

    ok=sparkles_sl_complete_movement(&session,2U,packet,
                                      sizeof packet,&used);
    CHECK(ok && used==46U);
    CHECK(ok && packet[9]==0xf9U &&
          !memcmp(packet+10U,session.agent,16U) &&
          !memcmp(packet+26U,session.session,16U));
    CHECK(ok && sparkles_sl_packet_view(packet,used,decoded,
          sizeof decoded,&view) && view.message_frequency==2U &&
          view.message_id==249U && view.sequence==2U);
    CHECK(!sparkles_sl_packet_view(packet,6U,decoded,
                                  sizeof decoded,&view));

    ok=sparkles_sl_im_encode(&session,agent,session_id,3U,
                            "MsSparkles Resident","hello",
                            packet,sizeof packet,&used);
    CHECK(ok && used>120U && packet[9U]==254U);
    CHECK(ok && sparkles_sl_packet_view(packet,used,decoded,
          sizeof decoded,&view) && view.message_frequency==2U &&
          view.message_id==254U && view.sequence==3U);
    CHECK(!sparkles_sl_im_encode(&session,agent,session_id,4U,
                                "MsSparkles Resident","",
                                packet,sizeof packet,&used));
    /* A typed operation outside an authenticated private IM must fail
     * closed even if its account text resembles an SL avatar UUID. */
    {
        sparkles_sl_group_start_request_t req={0};
        sparkles_sl_group_start_reply_t receipt={0};
        size_t receipt_used=9U;
        memcpy(req.account,"sl:12345678-1234-1234-1234-123456789abc",40U);
        memcpy(req.group,"STN-Labz",9U);
        memcpy(req.opening,"Hello everyone",15U);
        CHECK(group_start_service(&req,sizeof req,&receipt,sizeof receipt,
                                  &receipt_used,NULL)==
              STNLABZ_MODULE_ERR_NOT_AUTHORIZED);
        CHECK(!receipt.queued && receipt_used==0U);
        req.opening[0]=0;
        CHECK(group_start_service(&req,sizeof req,&receipt,sizeof receipt,
                                  &receipt_used,NULL)==
              STNLABZ_MODULE_ERR_INVALID_ARGUMENT);
    }

    /* Qualify the actual native grid HUD parser before activation. */
    {
        sparkles_sl_hud_request_t req={0};
        const char *owner="12345678-1234-1234-1234-123456789abc";
        CHECK(sparkles_sl_hud_parse(
            "STNGRID1|grid-1763776851-0|Sparkles%20Entertainment"
            "|STN-Labz%20Test|Visit%0Ahttps%3A%2F%2Fmaps.secondlife.com"
            "%2Fsecondlife%2FDongjak%2F214%2F230%2F52",owner,&req));
        CHECK(!strcmp(req.notice.subject,"STN-Labz Test"));
        CHECK(!strcmp(req.notice.body,
            "Visit\nhttps://maps.secondlife.com/secondlife/Dongjak/214/230/52"));
        CHECK(!strcmp(req.notice.group,"Sparkles Entertainment"));
        CHECK(req.landmark[0]==0);
        CHECK(!sparkles_sl_hud_parse(
            "STNGRID1|grid-1|Group|Title|Body|extra",owner,&req));
    }

    /* Object HUD commands cannot claim authority through their text:
     * only simulator-classified object IMs with both UUIDs are eligible. */
    {
        sparkles_sl_private_im_t hud={0};
        char owner_uuid[37]={0};
        unsigned char owner[16]={1U};
        memcpy(hud.sender,owner,16U);
        hud.conversation[0]=2U;
        hud.ambient=0U;
        CHECK(!hud_simulator_identity(&hud,owner_uuid));
        hud.ambient=3U;
        CHECK(hud_simulator_identity(&hud,owner_uuid));
        CHECK(!strcmp(owner_uuid,"01000000-0000-0000-0000-000000000000"));
        memset(hud.conversation,0,16U);
        CHECK(!hud_simulator_identity(&hud,owner_uuid));
    }
    {
        sparkles_sl_notice_send_request_t req={0};
        sparkles_sl_notice_send_reply_t receipt={0};
        size_t written=9U;
        memcpy(req.account,"sl:12345678-1234-1234-1234-123456789abc",40U);
        memcpy(req.group,"Sparkles Entertainment",23U);
        memcpy(req.subject,"Executive announcement",23U);
        memcpy(req.body,"Sparkles joins the team.",25U);
        CHECK(notice_send_service(&req,sizeof req,&receipt,
                                  sizeof receipt,&written,NULL)==
              STNLABZ_MODULE_ERR_NOT_AUTHORIZED);
        CHECK(!receipt.queued && written==0U);
        req.subject[0]='|';
        CHECK(notice_send_service(&req,sizeof req,&receipt,
                                  sizeof receipt,&written,NULL)==
              STNLABZ_MODULE_ERR_INVALID_ARGUMENT);
    }

#undef CHECK
    result->tests_executed=count;
    result->tests_passed=passed;
    result->tests_failed=count-passed;
    result->negative_test_executed=1;
    result->negative_test_passed=1;
    return count>=STNLABZ_MODULE_MIN_TESTS && count==passed ?
           STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
}
static stnlabz_module_result_t sl_start(const stnlabz_module_host_t *host)
{
    if (!host || !host->invoke_service || worker_started ||
        dispatcher_started) return STNLABZ_MODULE_ERR_START_FAILED;
    module_host=host;
    atomic_store(&stopping,0);
    atomic_store(&sl_present,0);
    if(!host->register_service || !host->unregister_service ||
       !host->register_service(SPARKLES_SL_GROUP_START_SERVICE,
                               group_start_service,NULL)) {
        module_host=NULL;
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    group_service_registered=1;
    if(!host->register_service(SPARKLES_SL_NOTICE_SEND_SERVICE,
                               notice_send_service,NULL)) {
        (void)host->unregister_service(SPARKLES_SL_GROUP_START_SERVICE,NULL);
        group_service_registered=0;
        module_host=NULL;
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    notice_service_registered=1;
    reset_queues(NULL);
    if (pthread_create(&dispatcher,NULL,dispatch_worker,NULL)!=0) {
        (void)host->unregister_service(SPARKLES_SL_NOTICE_SEND_SERVICE,NULL);
        notice_service_registered=0;
        (void)host->unregister_service(SPARKLES_SL_GROUP_START_SERVICE,NULL);
        group_service_registered=0;
        module_host=NULL;
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    dispatcher_started=1;
    if (pthread_create(&worker,NULL,grid_worker,NULL)!=0) {
        atomic_store(&stopping,1);
        pthread_mutex_lock(&qlock);
        pthread_cond_broadcast(&ready);
        pthread_mutex_unlock(&qlock);
        (void)pthread_join(dispatcher,NULL);
        dispatcher_started=0;
        (void)host->unregister_service(SPARKLES_SL_NOTICE_SEND_SERVICE,NULL);
        notice_service_registered=0;
        (void)host->unregister_service(SPARKLES_SL_GROUP_START_SERVICE,NULL);
        group_service_registered=0;
        module_host=NULL;
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    worker_started=1;
    if(pthread_create(&show_timer,NULL,show_timer_worker,NULL)!=0) {
        atomic_store(&stopping,1);
        pthread_mutex_lock(&qlock);
        pthread_cond_broadcast(&ready);
        pthread_mutex_unlock(&qlock);
        (void)pthread_join(worker,NULL);
        (void)pthread_join(dispatcher,NULL);
        worker_started=dispatcher_started=show_timer_started=0;
        (void)host->unregister_service(SPARKLES_SL_NOTICE_SEND_SERVICE,NULL);
        notice_service_registered=0;
        (void)host->unregister_service(SPARKLES_SL_GROUP_START_SERVICE,NULL);
        group_service_registered=0;
        module_host=NULL;
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    show_timer_started=1;
    fprintf(stderr,"[SL] native C grid/IM workers started; avatar presence not yet established\n");
    return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t sl_stop(void)
{
    int ok=1;
    if (!worker_started || !dispatcher_started || !show_timer_started)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    atomic_store(&stopping,1);
    pthread_mutex_lock(&qlock);
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&qlock);
    if (pthread_join(worker,NULL)!=0) ok=0;
    if (pthread_join(dispatcher,NULL)!=0) ok=0;
    if (pthread_join(show_timer,NULL)!=0) ok=0;
    if (!ok) return STNLABZ_MODULE_ERR_STOP_FAILED;
    worker_started=dispatcher_started=show_timer_started=0;
    atomic_store(&sl_present,0);
    reset_queues(NULL);
    if(notice_service_registered && module_host) {
        (void)module_host->unregister_service(SPARKLES_SL_NOTICE_SEND_SERVICE,NULL);
        notice_service_registered=0;
    }
    if(group_service_registered && module_host) {
        (void)module_host->unregister_service(SPARKLES_SL_GROUP_START_SERVICE,NULL);
        group_service_registered=0;
    }
    module_host=NULL;
    return STNLABZ_MODULE_OK;
}
static const stnlabz_module_descriptor_t descriptor={
    "sl","Sparkles Second Life Transport",SL_MAJOR,SL_MINOR,SL_PATCH,
    STNLABZ_MODULE_API_MAJOR,STNLABZ_MODULE_API_MINOR,
    sl_qualify,sl_start,sl_stop
};
const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
