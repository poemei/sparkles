#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "stnlabz_abi/module.h"
#include "sl_wire.h"
#include "sl_grid.h"

/* Native C in-process Second Life grid transport. Core controls lifecycle.
 * A module start means only that the worker started; grid login and avatar
 * presence require distinct, simulator-observed protocol milestones.
 * No IRC service ownership and no claimed SL identity from local IPC. */
#define SL_MAJOR 0U
#define SL_MINOR 2U
#define SL_PATCH 2U

static pthread_t worker;
static atomic_int stopping = ATOMIC_VAR_INIT(0);
static int worker_started;

static void *grid_worker(void *unused)
{
    (void)unused;
    sparkles_sl_grid_run(&stopping);
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
    if (!host || worker_started)
        return STNLABZ_MODULE_ERR_START_FAILED;
    atomic_store(&stopping,0);
    if (pthread_create(&worker,NULL,grid_worker,NULL)!=0)
        return STNLABZ_MODULE_ERR_START_FAILED;
    worker_started=1;
    fprintf(stderr,"[SL] native C grid worker started; avatar presence not yet established\n");
    return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t sl_stop(void)
{
    if (!worker_started) return STNLABZ_MODULE_ERR_INVALID_STATE;
    atomic_store(&stopping,1);
    if (pthread_join(worker,NULL)!=0)
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    worker_started=0;
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
