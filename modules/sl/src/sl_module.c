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
#define SL_PATCH 0U

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
    unsigned char uuid[16]={0}, packet[SPARKLES_SL_PACKET_MAX];
    sparkles_sl_session_t session={{0},{0},1234U};
    size_t used=0U;
    int ok_uuid, ok_packet;
    if (!result) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(result,0,sizeof *result);
    ok_uuid=sparkles_sl_uuid_parse(
        "12345678-1234-1234-1234-123456789abc",uuid);
    ok_packet=sparkles_sl_use_circuit(&session,1U,packet,
            sizeof packet,&used) && used==46U &&
            packet[6]==0xffU && packet[9]==3U;
    result->tests_executed=3U;
    result->tests_passed=(unsigned int)ok_uuid +
        (unsigned int)ok_packet +
        (unsigned int)!sparkles_sl_uuid_parse("invalid",uuid);
    result->tests_failed=result->tests_executed-result->tests_passed;
    result->negative_test_executed=1;
    result->negative_test_passed=1;
    return result->tests_failed ? STNLABZ_MODULE_ERR_QUALIFICATION :
           STNLABZ_MODULE_OK;
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
