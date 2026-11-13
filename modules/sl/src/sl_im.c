#define _POSIX_C_SOURCE 200809L
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
const char *sparkles_sl_im_validation_name(sparkles_sl_im_validation_t result)
{
    switch (result) {
    case SPARKLES_SL_IM_VALID: return "valid";
    case SPARKLES_SL_IM_TOO_SHORT: return "truncated";
    case SPARKLES_SL_IM_WRONG_RECIPIENT: return "wrong_recipient";
    case SPARKLES_SL_IM_UNSUPPORTED_DIALOG: return "unsupported_dialog";
    case SPARKLES_SL_IM_INVALID_SENDER: return "invalid_sender";
    case SPARKLES_SL_IM_INVALID_NAME: return "invalid_name";
    case SPARKLES_SL_IM_INVALID_TEXT: return "invalid_text";
    case SPARKLES_SL_IM_INVALID_BUCKET: return "invalid_bucket";
    case SPARKLES_SL_IM_TRAILING_DATA: return "trailing_data";
    default: return "unknown";
    }
}
sparkles_sl_im_validation_t sparkles_sl_im_validate(
    const unsigned char *p, size_t n,
    const unsigned char own_agent[16], sparkles_sl_private_im_t *im)
{
    size_t pos=103U, name_size, message_size, bucket_size;
    sparkles_sl_private_im_t candidate;
    if (!p || !own_agent || !im || n<108U)
        return SPARKLES_SL_IM_TOO_SHORT;
    memset(&candidate,0,sizeof candidate);
    /* message_template.msg ImprovedInstantMessage Low 254:
     * AgentData: AgentID(16), SessionID(16)
     * MessageBlock: FromGroup(1), ToAgentID(16), EstateID(4),
     * RegionID(16), Position(12), Offline(1), Dialog(1),
     * ID(16), Timestamp(4), Name(Variable1), Text(Variable2),
     * BinaryBucket(Variable2). Fixed portion totals 103 bytes.
     * No interpretation of unrelated IM dialog types as personal chat. */
    if (!nonzero_uuid(p) || !memcmp(p,own_agent,16U))
        return SPARKLES_SL_IM_INVALID_SENDER;
    /* Group relay may address the avatar or the active session UUID.
     * Never accept an arbitrary third-party destination. */
    if (memcmp(p+33U,own_agent,16U)!=0 &&
        !(p[82U]==SPARKLES_SL_SESSION_SEND_DIALOG &&
          nonzero_uuid(p+83U) &&
          memcmp(p+33U,p+83U,16U)==0))
        return SPARKLES_SL_IM_WRONG_RECIPIENT;
    if (p[82U]!=0U && p[82U]!=SPARKLES_SL_SESSION_SEND_DIALOG)
        return SPARKLES_SL_IM_UNSUPPORTED_DIALOG;
    if (p[82U]==0U && p[32U]!=0U)
        return SPARKLES_SL_IM_UNSUPPORTED_DIALOG;
    if (p[82U]==SPARKLES_SL_SESSION_SEND_DIALOG &&
        !nonzero_uuid(p+83U))
        return SPARKLES_SL_IM_UNSUPPORTED_DIALOG;
    memcpy(candidate.sender,p,16U);
    memcpy(candidate.conversation,p+83U,16U);
    candidate.ambient=p[82U]==SPARKLES_SL_SESSION_SEND_DIALOG?2U:0U;
    name_size=p[pos++];
    if (!name_size || name_size>sizeof candidate.name ||
        name_size>n-pos || p[pos+name_size-1U]!=0U ||
        memchr(p+pos,0U,name_size-1U))
        return SPARKLES_SL_IM_INVALID_NAME;
    memcpy(candidate.name,p+pos,name_size-1U);
    pos+=name_size;
    if (n-pos<2U) return SPARKLES_SL_IM_TOO_SHORT;
    message_size=(size_t)p[pos] | ((size_t)p[pos+1U]<<8U);
    pos+=2U;
    if (!message_size || message_size>sizeof candidate.text ||
        message_size>n-pos || p[pos+message_size-1U]!=0U ||
        memchr(p+pos,0U,message_size-1U))
        return SPARKLES_SL_IM_INVALID_TEXT;
    memcpy(candidate.text,p+pos,message_size-1U);
    if (!candidate.text[0]) return SPARKLES_SL_IM_INVALID_TEXT;
    pos+=message_size;
    if (n-pos<2U) return SPARKLES_SL_IM_TOO_SHORT;
    bucket_size=(size_t)p[pos] | ((size_t)p[pos+1U]<<8U);
    pos+=2U;
    if (bucket_size>n-pos) return SPARKLES_SL_IM_INVALID_BUCKET;
    pos+=bucket_size;
    /* Current Linden Lab message_template.msg appends EstateBlock
     * (EstateID U32) and MetaData Variable (one U8 count, then each
     * entry Data Variable2). Validate rather than rejecting live IMs.
     * Also recognize legacy packets ending immediately after Bucket. */
    if (pos<n) {
        unsigned int count, i;
        if (n-pos<5U) return SPARKLES_SL_IM_TRAILING_DATA;
        pos+=4U; /* EstateBlock.EstateID */
        count=p[pos++];
        for (i=0U;i<count;++i) {
            size_t data_size;
            if (n-pos<2U) return SPARKLES_SL_IM_TRAILING_DATA;
            data_size=(size_t)p[pos] | ((size_t)p[pos+1U]<<8U);
            pos+=2U;
            if (data_size>n-pos) return SPARKLES_SL_IM_TRAILING_DATA;
            pos+=data_size;
        }
    }
    if (pos!=n) return SPARKLES_SL_IM_TRAILING_DATA;
    *im=candidate;
    return SPARKLES_SL_IM_VALID;
}
/* ChatFromSimulator Low 139: variable1 name, UUID source/owner,
 * source_type, chat_type, audible, vector3 position, variable2 text.
 * Only avatar-origin normal/whisper/shout in audible range is context.
 * A nearby chat observation is not a request to reply. */
int sparkles_sl_chat_decode(const unsigned char *p,size_t n,
                           const unsigned char own_agent[16],
                           sparkles_sl_private_im_t *chat)
{
    size_t pos=0U,name_size,text_size,i;
    sparkles_sl_private_im_t candidate;
    if(!p||!own_agent||!chat||n<1U)return 0;
    memset(&candidate,0,sizeof candidate);
    name_size=p[pos++];
    if(name_size<2U||name_size>sizeof candidate.name||
       name_size>n-pos||p[pos+name_size-1U]!=0U||
       memchr(p+pos,0U,name_size-1U))return 0;
    for(i=0U;i<name_size-1U;++i)
        if(p[pos+i]<0x20U||p[pos+i]==0x7fU)return 0;
    memcpy(candidate.name,p+pos,name_size-1U);
    pos+=name_size;
    if(n-pos<16U+16U+3U+12U+2U)return 0;
    if(!nonzero_uuid(p+pos)||!memcmp(p+pos,own_agent,16U))return 0;
    memcpy(candidate.sender,p+pos,16U);
    pos+=32U; /* SourceID and OwnerID; only SourceID identifies avatar. */
    if(p[pos++]!=1U)return 0; /* Agent speech only; exclude objects/system. */
    if(p[pos]>2U)return 0; /* Whisper, normal, shout; exclude debug/typing. */
    ++pos;
    if(p[pos++]!=1U)return 0; /* Fully audible only. */
    pos+=12U; /* Position: not needed for contextual attribution. */
    text_size=(size_t)p[pos]|((size_t)p[pos+1U]<<8U);
    pos+=2U;
    if(text_size<2U||text_size>sizeof candidate.text||
       text_size!=n-pos||p[pos+text_size-1U]!=0U||
       memchr(p+pos,0U,text_size-1U))return 0;
    for(i=0U;i<text_size-1U;++i)
        if(p[pos+i]<0x20U||p[pos+i]==0x7fU)return 0;
    memcpy(candidate.text,p+pos,text_size-1U);
    candidate.ambient=1U;
    *chat=candidate;
    return 1;
}
int sparkles_sl_im_decode(const unsigned char *p, size_t n,
                         const unsigned char own_agent[16],
                         sparkles_sl_private_im_t *im)
{
    return sparkles_sl_im_validate(p,n,own_agent,im)==SPARKLES_SL_IM_VALID;
}
static int im_encode_dialog(const sparkles_sl_session_t *session,
                            const unsigned char recipient[16],
                            const unsigned char conversation[16],
                            uint32_t sequence, const char *sender_name,
                            const char *message,unsigned int dialog,
                            unsigned char *out, size_t cap, size_t *used)
{
    size_t textlen, namelen, offset=10U, total;
    /* Viewer group/session packet uses the one-byte empty binary bucket.
     * The previous encoder sent a zero-length bucket for every dialog. */
    const unsigned int bucketlen=dialog==0U?0U:1U;
    if (used) *used=0U;
    if (!session || !recipient || !conversation || !sender_name ||
        !message || !out || !used || !sequence ||
        !nonzero_uuid(recipient) ||
        (dialog!=0U && dialog!=SPARKLES_SL_SESSION_SEND_DIALOG &&
         dialog!=SPARKLES_SL_GROUP_START_DIALOG &&
         dialog!=SPARKLES_SL_GROUP_NOTICE_DIALOG))
        return 0;
    for (namelen=0U; namelen<255U && sender_name[namelen]; ++namelen) {}
    if (!namelen || namelen>=255U) return 0;
    for (textlen=0U; textlen<SPARKLES_SL_IM_TEXT_MAX && message[textlen];
         ++textlen) { /* bounded native C11 length */ }
    if ((!textlen && dialog!=SPARKLES_SL_GROUP_START_DIALOG) ||
        textlen>=SPARKLES_SL_IM_TEXT_MAX) return 0;
    /* Header(6) + Low ID(4) + fixed fields(103) + name(1) +
     * message size(2) + message NUL(1) + bucket size(2). */
    total=10U+103U+1U+namelen+1U+2U+textlen+1U+2U+
          bucketlen+5U;
    if (total>cap || total>SPARKLES_SL_PACKET_MAX) return 0;
    memset(out,0,total);
    out[0]=SPARKLES_SL_FLAG_RELIABLE;
    u32be(out+1U,sequence);
    out[6]=0xffU;out[7]=0xffU;out[8]=0U;out[9]=254U;
    memcpy(out+offset,session->agent,16U);
    memcpy(out+offset+16U,session->session,16U);
    /* Conversation ID and recipient are the group/session UUID for
     * SessionSend; direct IMs retain the original recipient UUID. */
    memcpy(out+offset+33U,recipient,16U);
    out[offset+82U]=(unsigned char)dialog;
    /* ParentEstateID, RegionID, Position = zero for client-originated IM;
     * Offline=0; Dialog=0; ID=original private conversation identifier. */
    if(dialog!=SPARKLES_SL_GROUP_NOTICE_DIALOG)
        memcpy(out+offset+83U,conversation,16U);
    offset+=103U;
    out[offset++]=(unsigned char)(namelen+1U);
    memcpy(out+offset,sender_name,namelen);
    offset+=namelen+1U;
    u16le(out+offset,(unsigned int)textlen+1U);
    offset+=2U;
    memcpy(out+offset,message,textlen);
    offset+=textlen+1U;
    u16le(out+offset,bucketlen);
    offset+=2U+bucketlen; /* NUL binary bucket is already zero-filled. */
    /* New template: EstateBlock.EstateID (4 bytes zero),
     * MetaData block count (1 byte zero). Already zero-filled. */
    offset+=5U;
    *used=offset;
    return 1;
}

int sparkles_sl_im_encode(const sparkles_sl_session_t *session,
                         const unsigned char recipient[16],
                         const unsigned char conversation[16],
                         uint32_t sequence,const char *sender_name,
                         const char *message,unsigned char *out,
                         size_t cap,size_t *used)
{
    return im_encode_dialog(session,recipient,conversation,sequence,
                            sender_name,message,0U,out,cap,used);
}
int sparkles_sl_group_start_encode(const sparkles_sl_session_t *session,
                                  const unsigned char group[16],
                                  uint32_t sequence,const char *sender_name,
                                  unsigned char *output,size_t capacity,
                                  size_t *used)
{
    return im_encode_dialog(session,group,group,sequence,
                            sender_name,"",SPARKLES_SL_GROUP_START_DIALOG,
                            output,capacity,used);
}
int sparkles_sl_session_encode(const sparkles_sl_session_t *session,
                             const unsigned char conversation[16],
                             uint32_t sequence,const char *sender_name,
                             const char *message,unsigned char *out,
                             size_t cap,size_t *used)
{
    return im_encode_dialog(session,conversation,conversation,sequence,
                            sender_name,message,SPARKLES_SL_SESSION_SEND_DIALOG,
                            out,cap,used);
}

/* Follows viewer send_group_notice: dialog 32, group recipient,
 * null IM ID and subject|message body with empty one-byte bucket.
 * Do not encode inventory attachments. */
int sparkles_sl_notice_encode(const sparkles_sl_session_t *session,
                             const unsigned char group[16],
                             uint32_t sequence,const char *sender_name,
                             const char *subject,const char *body,
                             unsigned char *output,size_t capacity,size_t *used)
{
    char message[512];
    size_t i,subject_len,body_len;
    if(used)*used=0U;
    if(!subject||!body||!group||!session||!sender_name||!output||!used)
        return 0;
    subject_len=strnlen(subject,65U);
    body_len=strnlen(body,449U);
    if(!subject_len||subject_len>=65U||!body_len||body_len>=449U||
       subject_len+body_len+1U>510U)return 0;
    /* A vertical bar terminates the subject on the viewer/region side.
     * Refuse ambiguous or control-byte notice content. */
    for(i=0U;i<subject_len;++i)
        if(subject[i]=='|'||subject[i]=='\n'||subject[i]=='\r')return 0;
    for(i=0U;i<body_len;++i)
        if(body[i]=='|'||body[i]=='\r')return 0;
    memcpy(message,subject,subject_len);
    message[subject_len]='|';
    memcpy(message+subject_len+1U,body,body_len);
    message[subject_len+body_len+1U]=0;
    return im_encode_dialog(session,group,group,sequence,sender_name,
        message,SPARKLES_SL_GROUP_NOTICE_DIALOG,output,capacity,used);
}
