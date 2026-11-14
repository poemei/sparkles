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
    input[82U]=SPARKLES_SL_OBJECT_IM_DIALOG;
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    assert(sparkles_sl_object_im_decode(input,size,own,&im));
    assert(im.ambient==3U && !memcmp(im.sender,sender,16U));
    input[32U]=1U;
    assert(!sparkles_sl_object_im_decode(input,size,own,&im));
    input[32U]=0U;
    input[33U]^=1U;
    assert(!sparkles_sl_object_im_decode(input,size,own,&im));
    input[33U]^=1U;
    input[82U]=15U;
    assert(!sparkles_sl_object_im_decode(input,size,own,&im));
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
    /* SessionSend dialog 17: distinguish shared group/conference speech.
     * Reject inventory and invitation control dialogs as conversation. */
    input[82U]=SPARKLES_SL_SESSION_SEND_DIALOG;
    assert(sparkles_sl_im_decode(input,size,own,&im));
    assert(im.ambient==2U && !memcmp(im.conversation,conversation,16U));
    input[32U]=1U;
    assert(sparkles_sl_im_decode(input,size,own,&im));
    memcpy(input+33U,conversation,16U);
    assert(sparkles_sl_im_decode(input,size,own,&im));
    input[33U]^=4U; /* Neither own UUID nor session UUID: reject. */
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    memcpy(input+33U,own,16U);
    input[32U]=0U;
    input[82U]=13U; /* Session invitation control is not chat. */
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[82U]=32U; /* Group notice not treated as chat. */
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    input[82U]=SPARKLES_SL_SESSION_SEND_DIALOG;
    memset(input+83U,0,16U);
    assert(!sparkles_sl_im_decode(input,size,own,&im));
    memcpy(input+83U,conversation,16U);
    input[82U]=0U;
    assert(sparkles_sl_im_decode(input,size,own,&im));
    assert(im.ambient==0U);
    /* Current template: EstateID U32 then MetaData block count U8. */
    input[118U]=7U;
    input[122U]=0U;
    assert(sparkles_sl_im_validate(input,123U,own,&im)==SPARKLES_SL_IM_VALID);
    /* One metadata block containing a 3-byte binary field. */
    input[122U]=1U;
    input[123U]=3U; input[124U]=0U;
    input[125U]=1U; input[126U]=2U; input[127U]=3U;
    assert(sparkles_sl_im_validate(input,128U,own,&im)==SPARKLES_SL_IM_VALID);
    assert(sparkles_sl_im_validate(input,127U,own,&im)==SPARKLES_SL_IM_TRAILING_DATA);
    assert(sparkles_sl_im_validate(input,119U,own,&im)==SPARKLES_SL_IM_TRAILING_DATA);
    input[32U]=1U;
    assert(sparkles_sl_im_validate(input,128U,own,&im)==SPARKLES_SL_IM_UNSUPPORTED_DIALOG);
    input[32U]=0U;
    input[33U]^=1U;
    assert(sparkles_sl_im_validate(input,128U,own,&im)==SPARKLES_SL_IM_WRONG_RECIPIENT);
    input[33U]^=1U;
    memcpy(session.agent,own,16U);
    memcpy(session.session,conversation,16U);
    assert(sparkles_sl_im_encode(&session,sender,conversation,42U,
                                "MsSparkles Resident","Hi",packet,sizeof packet,&used));
    assert(used==146U && packet[0]==SPARKLES_SL_FLAG_RELIABLE);
    assert(packet[6U]==0xffU && packet[7U]==0xffU &&
           packet[8U]==0U && packet[9U]==254U);
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.message_frequency==2U && view.message_id==254U);
    assert(view.sequence==42U && view.payload_size==136U);
    assert(!memcmp(view.payload,own,16U));
    assert(!memcmp(view.payload+33U,sender,16U));
    assert(!memcmp(view.payload+83U,conversation,16U));
    assert(view.payload[82U]==0U);
    assert(view.payload[103U]==20U);
    assert(!strcmp((const char *)(view.payload+104U),"MsSparkles Resident"));
    assert(view.payload[124U]==3U && view.payload[125U]==0U);
    assert(!strcmp((const char *)(view.payload+126U),"Hi"));
    /* Channel-zero nearby avatar speech uses ChatFromViewer Low 80;
     * it must not be encoded as a direct IM or group message. */
    assert(sparkles_sl_nearby_encode(&session,52U,"Hello nearby",
                                   packet,sizeof packet,&used));
    assert(packet[0]==SPARKLES_SL_FLAG_RELIABLE);
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.message_frequency==2U &&
           view.message_id==SPARKLES_SL_CHAT_SEND_MESSAGE_ID);
    assert(view.sequence==52U && view.payload_size==52U);
    assert(!memcmp(view.payload,own,16U));
    assert(!memcmp(view.payload+16U,conversation,16U));
    assert(view.payload[32U]==13U && view.payload[33U]==0U);
    assert(!strcmp((const char *)(view.payload+34U),"Hello nearby"));
    assert(view.payload[47U]==1U); /* normal chat */
    assert(view.payload[48U]==0U && view.payload[49U]==0U &&
           view.payload[50U]==0U && view.payload[51U]==0U);
    assert(!sparkles_sl_nearby_encode(&session,0U,"Hello",packet,sizeof packet,&used));
    assert(!sparkles_sl_nearby_encode(&session,53U,"",packet,sizeof packet,&used));
    assert(!sparkles_sl_nearby_encode(&session,53U,"Hello",packet,10U,&used));
    assert(sparkles_sl_session_encode(&session,conversation,43U,
              "MsSparkles Resident","Hi Mochi",packet,sizeof packet,&used));
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.message_id==SPARKLES_SL_IM_MESSAGE_ID);
    assert(view.payload[82U]==SPARKLES_SL_SESSION_SEND_DIALOG);
    assert(view.payload[135U]==1U && view.payload[136U]==0U);
    assert(view.payload[137U]==0U); /* One-byte empty binary bucket. */
    assert(!memcmp(view.payload+33U,conversation,16U));
    assert(!memcmp(view.payload+83U,conversation,16U));
    assert(sparkles_sl_group_start_encode(&session,conversation,44U,
              "MsSparkles Resident",packet,sizeof packet,&used));
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.message_id==SPARKLES_SL_IM_MESSAGE_ID);
    assert(view.payload[82U]==SPARKLES_SL_GROUP_START_DIALOG);
    assert(!memcmp(view.payload+33U,conversation,16U));
    assert(!memcmp(view.payload+83U,conversation,16U));
    assert(view.payload[124U]==1U && view.payload[125U]==0U);
    assert(view.payload[127U]==1U && view.payload[128U]==0U);
    assert(view.payload[129U]==0U); /* Group start bucket. */
    /* Official group notice is dialog 32, not a group chat message.
     * Viewer requires null IM ID and subject|body plus an empty bucket. */
    assert(sparkles_sl_notice_encode(&session,conversation,50U,
           "MsSparkles Resident","Show starts soon","The River at 20:00",
           packet,sizeof packet,&used));
    assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
    assert(view.payload[82U]==SPARKLES_SL_GROUP_NOTICE_DIALOG);
    assert(!memcmp(view.payload+33U,conversation,16U));
    assert(!memcmp(view.payload+83U,(unsigned char[16]){0},16U));
    assert(!strcmp((const char *)(view.payload+126U),
                   "Show starts soon|The River at 20:00"));
    /* Attached notice uses the viewer-compatible LLSD inventory bucket. */
    {
        unsigned char item[16]={0x8aU,0x3bU,0x29U,0x41U};
        const unsigned char *payload;
        size_t pos,bucket_len;
        char expected[256];
        char item_id[37],owner_id[37];
        sparkles_sl_uuid_format(item,item_id);
        sparkles_sl_uuid_format(session.agent,owner_id);
        assert(sparkles_sl_notice_landmark_encode(&session,conversation,56U,
               "MsSparkles Resident","River","Join us at 20:00",item,
               packet,sizeof packet,&used));
        assert(sparkles_sl_packet_view(packet,used,decoded,sizeof decoded,&view));
        payload=view.payload;
        assert(payload[82U]==SPARKLES_SL_GROUP_NOTICE_DIALOG);
        assert(!memcmp(payload+83U,(unsigned char[16]){0},16U));
        pos=103U+1U+(size_t)payload[103U]; /* name-length byte + name */
        pos+=2U+(size_t)payload[pos]+((size_t)payload[pos+1U]<<8U);
        bucket_len=(size_t)payload[pos]|((size_t)payload[pos+1U]<<8U);
        pos+=2U;
        assert(bucket_len>80U && pos+bucket_len+5U==view.payload_size);
        snprintf(expected,sizeof expected,"<uuid>%s</uuid>",item_id);
        assert(strstr((const char *)payload+pos,expected)!=NULL);
        snprintf(expected,sizeof expected,"<uuid>%s</uuid>",owner_id);
        assert(strstr((const char *)payload+pos,expected)!=NULL);
        assert(!sparkles_sl_notice_landmark_encode(&session,conversation,57U,
               "MsSparkles Resident","River","Join us",(unsigned char[16]){0},
               packet,sizeof packet,&used));
    }
    assert(!sparkles_sl_notice_encode(&session,conversation,51U,
           "MsSparkles Resident","Bad|Subject","The River",
           packet,sizeof packet,&used));
    assert(!sparkles_sl_notice_encode(&session,conversation,51U,
           "MsSparkles Resident","Show","Bad|body",
           packet,sizeof packet,&used));
    assert(!sparkles_sl_notice_encode(&session,conversation,51U,
           "MsSparkles Resident","Show","",
           packet,sizeof packet,&used));
    assert(!sparkles_sl_group_start_encode(&session,(unsigned char[16]){0},
                 45U,"MsSparkles Resident",packet,sizeof packet,&used));
    assert(!sparkles_sl_session_encode(&session,(unsigned char[16]){0},
                 44U,"MsSparkles Resident","Hi",packet,sizeof packet,&used));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,0U,
                                 "MsSparkles Resident","Hi",packet,sizeof packet,&used));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,1U,
                                 "MsSparkles Resident","",packet,sizeof packet,&used));
    assert(!sparkles_sl_im_encode(&session,sender,conversation,1U,
                                 "MsSparkles Resident","Hi",packet,10U,&used));
    /* ChatFromSimulator Low 139: one audible avatar statement. */
    {
        unsigned char chat_data[128]={0};
        size_t chat_len=60U;
        chat_data[0]=4U;
        memcpy(chat_data+1U,"Poe",4U);
        memcpy(chat_data+5U,sender,16U);
        chat_data[37U]=1U; /* Agent source, not object/system. */
        chat_data[38U]=1U; /* Normal chat. */
        chat_data[39U]=1U; /* Fully audible. */
        chat_data[52U]=6U;
        memcpy(chat_data+54U,"Hello",6U);
        assert(sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        assert(im.ambient==1U && !strcmp(im.name,"Poe") &&
               !strcmp(im.text,"Hello") &&
               !memcmp(im.sender,sender,16U));
        assert(!sparkles_sl_chat_decode(chat_data,chat_len-1U,own,&im));
        chat_data[37U]=2U; /* Object speech excluded. */
        assert(!sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        chat_data[37U]=1U;
        chat_data[38U]=4U; /* Typing notification excluded. */
        assert(!sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        chat_data[38U]=1U;
        chat_data[39U]=0U; /* Not fully audible. */
        assert(!sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        chat_data[39U]=1U;
        memcpy(chat_data+5U,own,16U); /* Own speech excluded. */
        assert(!sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        memcpy(chat_data+5U,sender,16U);
        chat_data[55U]=0U; /* Embedded NUL excluded. */
        assert(!sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
        chat_data[55U]='e';
        assert(sparkles_sl_chat_decode(chat_data,chat_len,own,&im));
    }
    puts("SL private IM and nearby chat codec regression: PASS");
    return 0;
}
