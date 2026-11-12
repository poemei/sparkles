#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl_wire.h"

int main(void)
{
    sparkles_sl_session_t s = {{0},{0},0x12345678U};
    unsigned char packet[1200], decoded[1200], uuid[16];
    char formatted[37];
    sparkles_sl_packet_view_t view;
    size_t n = 0U;
    unsigned int i;
    assert(sparkles_sl_uuid_parse("01234567-89ab-cdef-0123-456789abcdef",uuid));
    sparkles_sl_uuid_format(uuid,formatted);
    assert(!strcmp(formatted,"01234567-89ab-cdef-0123-456789abcdef"));
    assert(!sparkles_sl_uuid_parse("01234567-89ab-cdef-0123-456789abcdeg",uuid));
    assert(!sparkles_sl_uuid_parse("x",uuid));
    for(i=0;i<16;i++){s.agent[i]=(unsigned char)i;s.session[i]=(unsigned char)(i+16U);}
    assert(sparkles_sl_use_circuit(&s,1U,packet,sizeof packet,&n) && n==46U);
    assert(packet[0]==0x40U && packet[6]==0xffU && packet[9]==0x03U);
    assert(packet[10]==0x78U && packet[11]==0x56U && packet[12]==0x34U && packet[13]==0x12U);
    assert(!memcmp(packet+14U,s.session,16U) && !memcmp(packet+30U,s.agent,16U));
    assert(sparkles_sl_packet_view(packet,n,decoded,sizeof decoded,&view));
    assert(view.sequence==1U && view.message_frequency==2U && view.message_id==3U);
    assert(view.payload_size==36U);
    assert(sparkles_sl_complete_movement(&s,2U,packet,sizeof packet,&n) && n==46U);
    assert(packet[9]==0xf9U && !memcmp(packet+10U,s.agent,16U));
    assert(!memcmp(packet+26U,s.session,16U) && packet[42]==0x78U);
    assert(sparkles_sl_packet_view(packet,n,decoded,sizeof decoded,&view));
    assert(view.message_id==249U);
    /* A valid zerocoded single-byte high-frequency message with four zero bytes. */
    memset(packet,0,sizeof packet);
    packet[0]=0x80U;packet[4]=3U;packet[6]=0x04U;packet[7]=0U;packet[8]=4U;
    assert(sparkles_sl_packet_view(packet,9U,decoded,sizeof decoded,&view));
    assert(view.message_frequency==0U && view.message_id==4U && view.payload_size==4U);
    assert(!view.payload[0] && !view.payload[3]);
    packet[8]=0U;
    assert(!sparkles_sl_packet_view(packet,9U,decoded,sizeof decoded,&view));
    assert(!sparkles_sl_packet_view(packet,6U,decoded,sizeof decoded,&view));
    printf("SL native C wire codec regression: PASS\n");
    return 0;
}
