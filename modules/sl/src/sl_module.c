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
#include "stnlabz_abi/module.h"
#include "sl_wire.h"
#include "sl_grid.h"

/* Native C in-process Second Life grid transport. Core controls lifecycle.
 * A module start means only that the worker started; grid login and avatar
 * presence require distinct, simulator-observed protocol milestones.
 * No IRC service ownership and no claimed SL identity from local IPC. */
#define SL_MAJOR 0U
#define SL_MINOR 3U
#define SL_PATCH 9U

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
        /* Ambient local chat informs context but is not addressed to the
         * assistant; never dispatch it as a conversational command. */
        /* A group speaker may not invoke Core slash commands or private
         * tools; only addressed plain conversation reaches the chat module. */
        if(im.ambient==1U ||
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
        memcpy(reply.text,response.text,sizeof reply.text);
        pthread_mutex_lock(&qlock);
        if (!atomic_load(&stopping) && generation==epoch &&
            out_count<SL_OUTBOUND_CAP) {
            outbound[out_tail]=reply;
            out_tail=(out_tail+1U)%SL_OUTBOUND_CAP;
            ++out_count;
        } else {
            fprintf(stderr,"[SL] %s response discarded (disconnect or full queue)\n",
                    im.ambient==2U?"session chat":"private IM");
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
static void *grid_worker(void *unused)
{
    (void)unused;
    sparkles_sl_grid_run(&stopping,enqueue_private,
                         dequeue_reply,reset_queues,presence_update,NULL);
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
    reset_queues(NULL);
    if (pthread_create(&dispatcher,NULL,dispatch_worker,NULL)!=0) {
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
    worker_started=dispatcher_started=0;
    reset_queues(NULL);
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
