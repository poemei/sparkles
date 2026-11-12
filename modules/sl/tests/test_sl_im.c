#include "sl_im.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned char sender[16]={1U}, own[16]={2U}, conversation[16]={3U};
    unsigned char input[256]={0}, packet[SPARKLES_SL_PACKET_MAX]={0};
    unsigned char decoded[SPARKLES_SL_PACKET_MAX]={0};
    sparkles_sl_private_im_t im;
    sparkles_sl_packet_view_t view;
    sparkles_sl_session_t session={{0},{0},777U};
    size_t size=118U, used=0U;
    /* Synthetic server-to-viewer private IM body: two UUIDs, then
     * FromGroup/ToAgent/Estate/Region/Position/Offline/Dialog/IM-ID/time,
     * followed by name, message and empty binary bucket. */
    memcpy(input,sender,16U);
    memcpy(input+33U,own,16U);
    memcpy(input+83U,conversation,16U);
    input[103U]=4U;
    memcpy(input+104U,"Poe",4U);
    input[108U]=6U;
    memcpy(input+110U,"Hello",6U);
    assert(sparkles_sl_im_decode(input,size,own,&im));
    assert(!strcmp(im.name,"Poe") && !strcmp(im.text,"Hello"));
    assert(!memcmp(im.sender,sender,16U));
    assert(!memcmp(im.conversation,conversation,16U));
    assert(!sparkles_sl_im_decode(input,size-1U,own,&im));
    input[32U]=1U;
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[32U]=0U;
    input[82U]=15U;
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[82U]=0U;
    input[33U]^=1U;
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[33U]^=1U;
    input[115U]=1U; /* Missing trailing NUL. */
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[115U]=0U;
    input[111U]=0U;
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[111U]='e';
    assert(sparkles_sl_im_decode(input,size,own,&im));
    memcpy(session.agent,own,16U);
    memcpy(session.session,conversation,16U);
    assert(sparkles_sl_im_encode(&session,sender,conversation,42U,
                                "MsSparkles Resident","Hi",packet,sizeof packet,&used));
    assert(used==141U && packet[0]==SPARKLES_SL_FLAG_RELIABLE);
    assert(packet[6U]==0xffU && packet[7U]==0xffU &&
           packet[8U]==0U && packet[9U]==254U);
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.message_frequency==2U && view.message_id==254U);
    assert(view.sequence==42U && view.payload_size==131U);
    assert(!memcmp(view.payload,own,16U));
    assert(!memcmp(view.payload+33U,sender,16U));
    assert(!memcmp(view.payload+83U,conversation,16U));
    assert(view.payload[82U]==0U);
    assert(view.payload[103U]==20U);
    assert(!strcmp((const char *)(view.payload+104U),"MsSparkles Resident"));
    assert(view.payload[124U]==3U && view.payload[125U]==0U);
    assert(!strcmp((const char *)(view.payload+126U),"Hi"));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,0U,
                                 "MsSparkles Resident","Hi",packet,sizeof packet,&used));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,1U,
                                 "MsSparkles Resident","",packet,sizeof packet,&used));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,1U,
                                 "MsSparkles Resident","Hi",packet,10U,&used));
    puts("SL private IM codec regression: PASS");
    return 0;
}
