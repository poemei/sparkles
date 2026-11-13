#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "sl_grid.h"
#include "sl_login.h"
#include "sl_wire.h"
#include "sl_im.h"
#include "sl_caps.h"
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SL_RETRY_MS 1000U
#define SL_MAX_RESENDS 3U
#define SL_HANDSHAKE_TIMEOUT_MS 16000U
#define SL_IDLE_TIMEOUT_MS 90000U
#define SL_OUTSTANDING_IM 8U

typedef struct {
    unsigned char bytes[SPARKLES_SL_PACKET_MAX];
    size_t length;
    uint32_t sequence;
    unsigned int attempts;
    uint64_t sent_at;
    int pending;
    unsigned int session_chat; /* Outgoing destination classification only. */
} reliable_t;

static uint64_t tick_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0U;
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}
static void pause_stop(atomic_int *stop, unsigned int ms)
{
    unsigned int elapsed = 0U;
    while (!atomic_load(stop) && elapsed < ms) {
        unsigned int part = ms - elapsed;
        if (part > 200U) part = 200U;
        (void)poll(NULL, 0, (int)part);
        elapsed += part;
    }
}
static void report(const char *message)
{
    fprintf(stderr, "[SL] %s\n", message);
    fflush(stderr);
}
static void put_u32be(unsigned char *out, uint32_t val)
{
    out[0]=(unsigned char)(val>>24U);
    out[1]=(unsigned char)(val>>16U);
    out[2]=(unsigned char)(val>>8U);
    out[3]=(unsigned char)val;
}
static uint32_t read_u32le(const unsigned char *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1]<<8U) |
           ((uint32_t)in[2]<<16U) | ((uint32_t)in[3]<<24U);
}
static void put_u32le(unsigned char *out, uint32_t val)
{
    out[0]=(unsigned char)val;
    out[1]=(unsigned char)(val>>8U);
    out[2]=(unsigned char)(val>>16U);
    out[3]=(unsigned char)(val>>24U);
}
static int send_packet(int fd, const unsigned char *p, size_t n)
{
    return send(fd, p, n, MSG_NOSIGNAL) == (ssize_t)n;
}
static int send_ack(int fd, uint32_t seq, uint32_t *next)
{
    unsigned char msg[15] = {0};
    /* PacketAck Fixed 0xFFFFFFFB; one U32 acknowledgment (little endian). */
    put_u32be(msg+1U, (*next)++);
    msg[6]=0xffU; msg[7]=0xffU; msg[8]=0xffU; msg[9]=0xfbU;
    msg[10]=1U;
    put_u32le(msg+11U, seq);
    return send_packet(fd,msg,sizeof msg);
}
static int send_ping_reply(int fd, unsigned char ping, uint32_t *next)
{
    unsigned char msg[8] = {0};
    put_u32be(msg+1U,(*next)++);
    msg[6]=2U; /* CompletePingCheck High 2 */
    msg[7]=ping;
    return send_packet(fd,msg,sizeof msg);
}
static int send_ping(int fd, unsigned char ping, uint32_t *next)
{
    unsigned char msg[12] = {0};
    uint32_t seq=(*next)++;
    put_u32be(msg+1U,seq);
    msg[6]=1U; /* StartPingCheck High 1 */
    msg[7]=ping;
    put_u32le(msg+8U,seq-1U);
    return send_packet(fd,msg,sizeof msg);
}
static void clear_ack(reliable_t *packet, uint32_t seq)
{
    if (packet->pending && packet->sequence == seq) {
        if(packet->session_chat)
            report("group session reply UDP transport acknowledged; group display still unverified");
        packet->pending=0;
    }
}
static void read_acknowledgments(const unsigned char *raw, size_t size,
                                 const sparkles_sl_packet_view_t *view,
                                 reliable_t *circuit, reliable_t *movement,
                                 reliable_t *action_start,
                                 reliable_t outgoing[SL_OUTSTANDING_IM])
{
    size_t i, offset;
    unsigned int count;
    if ((raw[0] & 0x10U) && size > 7U) {
        count=raw[size-1U];
        if (count && (size_t)count <= (size-7U)/4U) {
            offset=size-1U-(size_t)count*4U;
            for (i=0U;i<count;++i) {
                uint32_t seq=read_u32le(raw+offset+i*4U);
                size_t j;
                clear_ack(circuit,seq);clear_ack(movement,seq);
                clear_ack(action_start,seq);
                for (j=0U;j<SL_OUTSTANDING_IM;++j)
                    clear_ack(&outgoing[j],seq);
            }
        }
    }
    if (view->message_frequency==2U && view->message_id==65531U &&
        view->payload_size>=1U) {
        count=view->payload[0];
        if ((size_t)count <= (view->payload_size-1U)/4U) {
            for (i=0U;i<count;++i) {
                uint32_t seq=read_u32le(view->payload+1U+i*4U);
                size_t j;
                clear_ack(circuit,seq);clear_ack(movement,seq);
                clear_ack(action_start,seq);
                for (j=0U;j<SL_OUTSTANDING_IM;++j)
                    clear_ack(&outgoing[j],seq);
            }
        }
    }
}
static int send_reliable(int fd, reliable_t *slot,
                         const sparkles_sl_session_t *session,
                         int movement, uint32_t *next)
{
    size_t n = 0U;
    uint32_t seq=(*next)++;
    int ok=movement ? sparkles_sl_complete_movement(session,seq,slot->bytes,
                      sizeof slot->bytes,&n) :
                    sparkles_sl_use_circuit(session,seq,slot->bytes,
                      sizeof slot->bytes,&n);
    if (!ok || !send_packet(fd,slot->bytes,n)) return 0;
    slot->length=n;
    slot->sequence=seq;
    slot->attempts=0U;
    slot->sent_at=tick_ms();
    slot->pending=1;
    return 1;
}
static int resend(int fd, reliable_t *slot, uint64_t now)
{
    if (!slot->pending || now-slot->sent_at < SL_RETRY_MS) return 1;
    if (slot->attempts >= SL_MAX_RESENDS) return 0;
    /* Retransmissions carry the Resent flag and preserve sequence ID. */
    slot->bytes[0] |= 0x20U;
    if (!send_packet(fd,slot->bytes,slot->length)) return 0;
    slot->sent_at=now;
    ++slot->attempts;
    return 1;
}
static void put_f32le(unsigned char *out, float value)
{
    uint32_t bits;
    memcpy(&bits,&value,sizeof bits);
    put_u32le(out,bits);
}
static int send_agent_update(int fd,
                             const sparkles_sl_session_t *session,
                             const unsigned char position[12],
                             uint32_t *next)
{
    unsigned char msg[121]={0};
    put_u32be(msg+1U,(*next)++);
    msg[6]=4U; /* AgentUpdate High 4, uncompressed valid payload. */
    memcpy(msg+7U,session->agent,16U);
    memcpy(msg+23U,session->session,16U);
    /* Body/head rotations are identity quaternions, encoded (0,0,0).
     * A neutral observer orientation and zero control flags mean idle. */
    memcpy(msg+64U,position,12U);
    put_f32le(msg+76U,1.0f);  /* CameraAtAxis X */
    put_f32le(msg+92U,1.0f);  /* CameraLeftAxis Y */
    put_f32le(msg+108U,1.0f); /* CameraUpAxis Z */
    put_f32le(msg+112U,64.0f);/* CameraFar */
    return send_packet(fd,msg,sizeof msg);
}
static int send_region_reply(int fd, const sparkles_sl_session_t *session,
                             uint32_t *next)
{
    unsigned char msg[46]={0};
    put_u32be(msg+1U,(*next)++);
    msg[6]=0xffU; msg[7]=0xffU; msg[9]=149U;
    memcpy(msg+10U,session->agent,16U);
    memcpy(msg+26U,session->session,16U);
    /* RegionInfo.Flags is zero; no unsupported viewer features advertised. */
    return send_packet(fd,msg,sizeof msg);
}
/* A logged-in simulator is trusted only through the connected socket and
 * an AgentMovementComplete bearing both login-issued UUIDs. */
static int grid_session(const sparkles_sl_login_reply_t *login,
                        atomic_int *stop,
                        sparkles_sl_im_ingress_fn ingress,
                        sparkles_sl_im_dequeue_fn dequeue,
                        sparkles_sl_presence_fn presence,
                        void *context)
{
    struct sockaddr_in sim = {0};
    unsigned char raw[SPARKLES_SL_PACKET_MAX], decoded[SPARKLES_SL_PACKET_MAX];
    reliable_t circuit={0}, movement={0}, action_start={0};
    sparkles_sl_im_reply_t pending_action={0};
    int pending_active=0, group_event_status=0;
    uint64_t action_started_at=0U;
    unsigned char group_event_uuid[16]={0};
    unsigned char active_groups[64][16]={{0}};
    unsigned int active_group_count=0U;
    pthread_mutex_t group_lock=PTHREAD_MUTEX_INITIALIZER;
    reliable_t outbound[SL_OUTSTANDING_IM]={0};
    uint32_t next=1U, recently_seen[128]={0};
    unsigned char ping=0U;
    unsigned char avatar_position[12]={0};
    uint64_t began, last_recv, last_ping, last_update;
    int fd, started_movement=0, present=0, caps_started=0;
    pthread_t caps_thread;
    sparkles_sl_caps_worker_t caps={0};
    sim.sin_family=AF_INET;
    sim.sin_port=htons(login->sim_port);
    if (inet_pton(AF_INET,login->sim_ip,&sim.sin_addr)!=1) return 0;
    fd=socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd<0) return 0;
    if (connect(fd,(struct sockaddr *)&sim,sizeof sim)!=0 ||
        !send_reliable(fd,&circuit,&login->circuit,0,&next)) {
        close(fd);
        return 0;
    }
    report("simulator circuit requested; awaiting simulator traffic");
    began=last_recv=last_ping=last_update=tick_ms();
    while (!atomic_load(stop)) {
        struct pollfd p={fd,POLLIN,0};
        int ready=poll(&p,1,200);
        uint64_t now=tick_ms();
        if (ready<0 && errno==EINTR) continue;
        if (ready<0 || (ready>0 && (p.revents&(POLLERR|POLLNVAL)))) break;
        if (ready>0 && (p.revents&POLLIN)) {
            sparkles_sl_packet_view_t view;
            ssize_t n=recv(fd,raw,sizeof raw,0);
            size_t i;
            int duplicate=0;
            if (n<=0 || !sparkles_sl_packet_view(raw,(size_t)n,decoded,
                                       sizeof decoded,&view)) continue;
            last_recv=now;
            read_acknowledgments(raw,(size_t)n,&view,&circuit,&movement,
                                 &action_start,outbound);
            if ((view.flags & SPARKLES_SL_FLAG_RELIABLE) &&
                !send_ack(fd,view.sequence,&next)) break;
            for (i=0U;i<128U;++i) {
                if (view.sequence!=0U && recently_seen[i]==view.sequence) {duplicate=1;break;}
            }
            if (duplicate) continue;
            recently_seen[view.sequence % 128U]=view.sequence;
            if (view.message_frequency==2U &&
                view.message_id==SPARKLES_SL_IM_MESSAGE_ID) {
                sparkles_sl_private_im_t im;
                sparkles_sl_im_validation_t status;
                report("ImprovedInstantMessage UDP packet received");
                if (view.payload_size>82U &&
                    view.payload[82U]>=13U && view.payload[82U]<=16U)
                    report("session invitation/control packet observed; event worker handles joining");
                if (!present || !ingress) {
                    report("IM packet ignored: avatar session not ready");
                } else {
                    status=sparkles_sl_im_validate(view.payload,
                                   view.payload_size,login->circuit.agent,&im);
                    if (status==SPARKLES_SL_IM_VALID) {
                        if (ingress(&im,context))
                            report(im.ambient==2U?
                                   "session chat accepted into Core dispatch queue":
                                   "private IM accepted into Core dispatch queue");
                    } else {
                        fprintf(stderr,"[SL] IM packet rejected: reason=%s length=%zu dialog=%u\n",
                                sparkles_sl_im_validation_name(status),
                                view.payload_size,
                                view.payload_size>82U?(unsigned)view.payload[82U]:0U);
                        fflush(stderr);
                    }
                }
            }
            /* Nearby avatar chat is observational only: never route to
             * conversational reply generation. Packet deduplication and
             * the authenticated simulator circuit apply as for IMs. */
            if (view.message_frequency==2U &&
                view.message_id==SPARKLES_SL_CHAT_MESSAGE_ID && present &&
                ingress) {
                sparkles_sl_private_im_t chat;
                if(sparkles_sl_chat_decode(view.payload,view.payload_size,
                                           login->circuit.agent,&chat)) {
                    if(!ingress(&chat,context))
                        report("nearby chat observation queue unavailable");
                }
            }
            if (view.message_frequency==0U && view.message_id==1U &&
                view.payload_size>=5U &&
                !send_ping_reply(fd,view.payload[0],&next)) break;
            if (!started_movement) {
                if (!send_reliable(fd,&movement,&login->circuit,1,&next))
                    break;
                started_movement=1;
                report("simulator responded; CompleteAgentMovement sent");
            }
            if (view.message_frequency==2U && view.message_id==148U &&
                !send_region_reply(fd,&login->circuit,&next)) break;
            if (view.message_frequency==2U && view.message_id==250U &&
                view.payload_size>=32U &&
                !memcmp(view.payload,login->circuit.agent,16U) &&
                !memcmp(view.payload+16U,login->circuit.session,16U)) {
                if (view.payload_size>=44U)
                    memcpy(avatar_position,view.payload+32U,12U);
                if (!present) {
                    if (!send_agent_update(fd,&login->circuit,avatar_position,&next))
                        break;
                    last_update=now;
                    circuit.pending=0;
                    movement.pending=0;
                    present=1;
                    if(presence)presence(1,context);
                    report("simulator confirmed AgentMovementComplete; avatar presence observed");
                    if(strlen(login->seed_capability)<sizeof caps.seed_url) {
                        memcpy(caps.seed_url,login->seed_capability,
                               strlen(login->seed_capability)+1U);
                        memcpy(caps.agent,login->circuit.agent,16U);
                        caps.module_stop=stop;
                        caps.ingress=ingress;
                        caps.context=context;
                        caps.group_lock=&group_lock;
                        caps.group_confirm_uuid=group_event_uuid;
                        caps.group_confirm_status=&group_event_status;
                        caps.shared_joined=active_groups;
                        caps.shared_joined_count=&active_group_count;
                        atomic_init(&caps.session_stop,0);
                        if(pthread_create(&caps_thread,NULL,sparkles_sl_caps_run,&caps)==0)
                            caps_started=1;
                        else report("capability event worker could not start");
                    }
                }
            }
        }
        if (!started_movement && !circuit.pending) {
            if (!send_reliable(fd,&movement,&login->circuit,1,&next)) break;
            started_movement=1;
            report("circuit acknowledged; CompleteAgentMovement sent");
        }
        if (!present && now-began >= SL_HANDSHAKE_TIMEOUT_MS) {
            report("simulator presence handshake timed out");
            break;
        }
        if (now-last_recv >= SL_IDLE_TIMEOUT_MS) {
            report("simulator silent; reconnecting");
            break;
        }
        if (present && now-last_update >= 3000U) {
            if (!send_agent_update(fd,&login->circuit,avatar_position,&next)) break;
            last_update=now;
        }
        if (present && now-last_ping >= 10000U) {
            if (!send_ping(fd,ping++,&next)) break;
            last_ping=now;
        }
        if (present && pending_active) {
            int completed=0,success=0;
            if(now-action_started_at>30000U) {
                pending_active=0;
                action_start.pending=0;
                memset(&pending_action,0,sizeof pending_action);
                report("DM-directed group start timed out; opening NOT sent");
            }
            if(!pending_active)continue;
            pthread_mutex_lock(&group_lock);
            if(group_event_status &&
               !memcmp(group_event_uuid,pending_action.conversation,16U)) {
                completed=1;
                success=group_event_status>0;
                group_event_status=0;
            }
            pthread_mutex_unlock(&group_lock);
            if(completed) {
                if(success)
                    report("DM-directed group start confirmed; opening queued for session send");
                else
                    report("DM-directed group start rejected; opening NOT sent");
                if(success)pending_action.start_group=2U;
                pending_active=0;
            }
        }
        if (present && dequeue) {
            sparkles_sl_im_reply_t response;
            size_t j;
            /* Never monopolize the socket loop: a fixed queue, fixed packet
             * size and bounded retransmission keep grid traffic responsive. */
            for (j=0U;j<SL_OUTSTANDING_IM;++j) {
                size_t used=0U;
                reliable_t *slot;
                if(atomic_load(stop))break;
                if(pending_action.start_group==2U) {
                    size_t free_slots=0U,k;
                    for(k=0U;k<SL_OUTSTANDING_IM;++k)
                        if(!outbound[k].pending)++free_slots;
                    if(!free_slots)break; /* Keep opening queued. */
                    response=pending_action;
                    memset(&pending_action,0,sizeof pending_action);
                    response.start_group=0U;
                } else if(!dequeue(&response,context))break;
                if(response.start_group==1U) {
                    size_t start_size=0U,k;
                    int already_joined=0;
                    pthread_mutex_lock(&group_lock);
                    for(k=0U;k<active_group_count;++k)
                        if(!memcmp(active_groups[k],response.conversation,16U)) {
                            already_joined=1;
                            break;
                        }
                    pthread_mutex_unlock(&group_lock);
                    if(already_joined) {
                        pending_action=response;
                        pending_action.start_group=2U;
                        report("DM-directed group session already joined; opening queued");
                        continue;
                    } else {
                    if(pending_active || action_start.pending) {
                        report("group start already in progress; requested opening not sent");
                        continue;
                    }
                    if(!sparkles_sl_group_start_encode(&login->circuit,
                          response.conversation,next,login->avatar_name,
                          action_start.bytes,sizeof action_start.bytes,
                          &start_size) ||
                       !send_packet(fd,action_start.bytes,start_size)) {
                        report("DM-directed group start packet failed; opening not sent");
                        continue;
                    }
                    action_start.sequence=next++;
                    action_start.length=start_size;
                    action_start.sent_at=now;
                    action_start.attempts=0U;
                    action_start.pending=1;
                    pending_action=response;
                    pending_active=1;
                    action_started_at=now;
                    report("DM-directed group start requested; awaiting session acceptance before opening");
                    continue;
                    }
                }
                slot=&outbound[j];
                if (slot->pending) {
                    size_t k;
                    for (k=0U;k<SL_OUTSTANDING_IM;++k)
                        if (!outbound[k].pending) {slot=&outbound[k];break;}
                    if (k==SL_OUTSTANDING_IM) {
                        report("outbound IM window full; response dropped");
                        break;
                    }
                }
                if (!(response.notice ?
                      sparkles_sl_notice_encode(&login->circuit,
                          response.recipient,next,login->avatar_name,
                          response.subject,response.text,
                          slot->bytes,sizeof slot->bytes,&used) :
                      response.session_chat ?
                      sparkles_sl_session_encode(&login->circuit,
                          response.conversation,next,login->avatar_name,
                          response.text,slot->bytes,sizeof slot->bytes,&used) :
                      sparkles_sl_im_encode(&login->circuit,response.recipient,
                          response.conversation,next,login->avatar_name,
                          response.text,slot->bytes,sizeof slot->bytes,&used))) {
                    report(response.notice?"group notice encoding rejected":
                          response.session_chat?"group session reply encoding rejected":
                                                "private IM reply encoding rejected");
                    continue;
                }
                if (!send_packet(fd,slot->bytes,used)) {
                    report(response.notice?"group notice UDP send failed":
                           response.session_chat?"group session reply UDP send failed":
                                                 "private IM reply UDP send failed");
                    break;
                }
                slot->length=used;
                slot->sequence=next++;
                slot->attempts=0U;
                slot->sent_at=now;
                slot->pending=1;
                slot->session_chat=response.session_chat;
                report(response.notice?
                    "official group notice sent via UDP dialog 32; delivery NOT confirmed":
                    response.session_chat?
                    "group session reply sent via UDP dialog 17; awaiting simulator acknowledgment":
                    "private IM reply sent via UDP dialog 0; awaiting simulator acknowledgment");
            }
        }
        {
            size_t j;
            for (j=0U;j<SL_OUTSTANDING_IM;++j) {
                if (outbound[j].pending && !resend(fd,&outbound[j],now)) {
                    outbound[j].pending=0;
                    report(outbound[j].session_chat?
                           "group session reply unacknowledged; retry limit reached":
                           "private IM reply unacknowledged; retry limit reached");
                }
            }
        }
        if(action_start.pending && !resend(fd,&action_start,now)) {
            action_start.pending=0;
            report("DM-directed group start transport acknowledgment timed out");
        }
        if (!resend(fd,&circuit,now) || !resend(fd,&movement,now)) {
            report("simulator reliable handshake acknowledgment timed out");
            break;
        }
    }
    if(caps_started)atomic_store(&caps.session_stop,1);
    if(caps_started)(void)pthread_join(caps_thread,NULL);
    pthread_mutex_destroy(&group_lock);
    close(fd);
    if (presence)presence(0,context);
    if (present) report("simulator circuit closed; avatar presence lost");
    return present;
}
void sparkles_sl_grid_run(atomic_int *stop,
                          sparkles_sl_im_ingress_fn ingress,
                          sparkles_sl_im_dequeue_fn dequeue,
                          sparkles_sl_im_reset_fn reset,
                          sparkles_sl_presence_fn presence,
                          void *context)
{
    while (!atomic_load(stop)) {
        sparkles_sl_login_reply_t login={0};
        if (presence)presence(0,context);
        if (reset) reset(context);
        if (!sparkles_sl_login_from_file(&login)) {
            report("grid login failed (credentials, protocol, or network); no avatar presence");
        } else {
            report("grid login response accepted; attempting simulator circuit");
            (void)grid_session(&login,stop,ingress,dequeue,presence,context);
        }
        memset(&login,0,sizeof login);
        if (!atomic_load(stop)) pause_stop(stop,10000U);
    }
}
