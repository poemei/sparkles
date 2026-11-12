#include "sl_im.h"
#include <string.h>

static void u16le(unsigned char *p, unsigned int x)
{
    p[0]=(unsigned char)x;
    p[1]=(unsigned char)(x>>8U);
}
static void u32be(unsigned char *p, uint32_t x)
{
    p[0]=(unsigned char)(x>>24U);
    p[1]=(unsigned char)(x>>16U);
    p[2]=(unsigned char)(x>>8U);
    p[3]=(unsigned char)x;
}
static int nonzero_uuid(const unsigned char *p)
{
    size_t i;
    unsigned char any=0U;
    for(i=0U;i<16U;++i) any|=p[i];
    return any!=0U;
}
int sparkles_sl_im_decode(const unsigned char *p, size_t n,
                         const unsigned char own_agent[16],
                         sparkles_sl_private_im_t *im)
{
    size_t pos=103U, name_size, message_size, bucket_size;
    sparkles_sl_private_im_t candidate;
    if (!p || !own_agent || !im || n<108U) return 0;
    memset(&candidate,0,sizeof candidate);
    /* AgentData.AgentID is the claimed sender UUID provided by the
     * simulator. MessageBlock.ToAgentID must match our login agent UUID.
     * Dialog 0 is normal personal IM, and FromGroup must be false. */
    if (!nonzero_uuid(p) || !memcmp(p,own_agent,16U) ||
        p[32U]!=0U || memcmp(p+33U,own_agent,16U)!=0 ||
        p[82U]!=0U) return 0;
    memcpy(candidate.sender,p,16U);
    memcpy(candidate.conversation,p+83U,16U);
    name_size=p[pos++];
    if (!name_size || name_size>sizeof candidate.name ||
        name_size>n-pos) return 0;
    if (p[pos+name_size-1U]!=0U ||
        memchr(p+pos,0U,name_size-1U)) return 0;
    memcpy(candidate.name,p+pos,name_size-1U);
    pos+=name_size;
    if (n-pos<2U) return 0;
    message_size=(size_t)p[pos] | ((size_t)p[pos+1U]<<8U);
    pos+=2U;
    if (!message_size || message_size>sizeof candidate.text ||
        message_size>n-pos) return 0;
    /* Message strings are NUL terminated in SL wire format. */
    if (p[pos+message_size-1U]!=0U ||
        memchr(p+pos,0U,message_size-1U)) return 0;
    memcpy(candidate.text,p+pos,message_size-1U);
    if (!candidate.text[0]) return 0;
    pos+=message_size;
    if (n-pos<2U) return 0;
    bucket_size=(size_t)p[pos] | ((size_t)p[pos+1U]<<8U);
    pos+=2U;
    if (bucket_size!=n-pos) return 0;
    *im=candidate;
    return 1;
}
int sparkles_sl_im_encode(const sparkles_sl_session_t *session,
                         const unsigned char recipient[16],
                         const unsigned char conversation[16],
                         uint32_t sequence, const char *message,
                         unsigned char *out, size_t cap, size_t *used)
{
    size_t textlen, offset=10U, total;
    if (used) *used=0U;
    if (!session || !recipient || !conversation || !message || !out ||
        !used || !sequence || !nonzero_uuid(recipient)) return 0;
    textlen=strnlen(message,SPARKLES_SL_IM_TEXT_MAX);
    if (!textlen || textlen>=SPARKLES_SL_IM_TEXT_MAX) return 0;
    /* Header(6) + Low ID(4) + fixed fields(103) + name(1) +
     * message size(2) + message NUL(1) + bucket size(2). */
    total=10U+103U+1U+2U+textlen+1U+2U;
    if (total>cap || total>SPARKLES_SL_PACKET_MAX) return 0;
    memset(out,0,total);
    out[0]=SPARKLES_SL_FLAG_RELIABLE;
    u32be(out+1U,sequence);
    out[6]=0xffU;out[7]=0xffU;out[8]=0U;out[9]=254U;
    memcpy(out+offset,session->agent,16U);
    memcpy(out+offset+16U,session->session,16U);
    /* FromGroup BOOL = 0 */
    memcpy(out+offset+33U,recipient,16U);
    /* ParentEstateID, RegionID, Position = zero for client-originated IM;
     * Offline=0; Dialog=0; ID=original private conversation identifier. */
    memcpy(out+offset+83U,conversation,16U);
    offset+=103U;
    out[offset++]=0U; /* empty FromAgentName (simulator supplies identity) */
    u16le(out+offset,(unsigned int)textlen+1U);
    offset+=2U;
    memcpy(out+offset,message,textlen);
    offset+=textlen+1U;
    u16le(out+offset,0U);
    offset+=2U;
    *used=offset;
    return 1;
}
