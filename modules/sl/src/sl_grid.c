#define _POSIX_C_SOURCE 200809L
#include "sl_grid.h"
#include "sl_login.h"
#include "sl_wire.h"
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

typedef struct {
    unsigned char bytes[SPARKLES_SL_PACKET_MAX];
    size_t length;
    uint32_t sequence;
    unsigned int attempts;
    uint64_t sent_at;
    int pending;
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
    if (packet->pending && packet->sequence == seq) packet->pending=0;
}
static void read_acknowledgments(const unsigned char *raw, size_t size,
                                 const sparkles_sl_packet_view_t *view,
                                 reliable_t *circuit, reliable_t *movement)
{
    size_t i, offset;
    unsigned int count;
    if ((raw[0] & 0x10U) && size > 7U) {
        count=raw[size-1U];
        if (count && (size_t)count <= (size-7U)/4U) {
            offset=size-1U-(size_t)count*4U;
            for (i=0U;i<count;++i) {
                uint32_t seq=read_u32le(raw+offset+i*4U);
                clear_ack(circuit,seq);clear_ack(movement,seq);
            }
        }
    }
    if (view->message_frequency==2U && view->message_id==65531U &&
        view->payload_size>=1U) {
        count=view->payload[0];
        if ((size_t)count <= (view->payload_size-1U)/4U) {
            for (i=0U;i<count;++i) {
                uint32_t seq=read_u32le(view->payload+1U+i*4U);
                clear_ack(circuit,seq);clear_ack(movement,seq);
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
    if (!send_packet(fd,slot->bytes,slot->length)) return 0;
    slot->sent_at=now;
    ++slot->attempts;
    return 1;
}
/* A logged-in simulator is trusted only through the connected socket and
 * an AgentMovementComplete bearing both login-issued UUIDs. */
static int grid_session(const sparkles_sl_login_reply_t *login,
                        atomic_int *stop)
{
    struct sockaddr_in sim = {0};
    unsigned char raw[SPARKLES_SL_PACKET_MAX], decoded[SPARKLES_SL_PACKET_MAX];
    reliable_t circuit={0}, movement={0};
    uint32_t next=1U, recently_seen[128]={0};
    size_t seen_count=0U;
    unsigned char ping=0U;
    uint64_t began, last_recv, last_ping;
    int fd, started_movement=0, present=0;
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
    began=last_recv=last_ping=tick_ms();
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
            read_acknowledgments(raw,(size_t)n,&view,&circuit,&movement);
            if ((view.flags & SPARKLES_SL_FLAG_RELIABLE) &&
                !send_ack(fd,view.sequence,&next)) break;
            for (i=0U;i<seen_count;++i) {
                if (recently_seen[i]==view.sequence) {duplicate=1;break;}
            }
            if (duplicate) continue;
            recently_seen[view.sequence % 128U]=view.sequence;
            if (seen_count<128U) ++seen_count;
            if (view.message_frequency==0U && view.message_id==1U &&
                view.payload_size>=5U &&
                !send_ping_reply(fd,view.payload[0],&next)) break;
            if (!started_movement) {
                if (!send_reliable(fd,&movement,&login->circuit,1,&next))
                    break;
                started_movement=1;
                report("simulator responded; CompleteAgentMovement sent");
            }
            if (view.message_frequency==2U && view.message_id==250U &&
                view.payload_size>=32U &&
                !memcmp(view.payload,login->circuit.agent,16U) &&
                !memcmp(view.payload+16U,login->circuit.session,16U)) {
                if (!present) {
                    present=1;
                    report("simulator confirmed AgentMovementComplete; avatar presence observed");
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
        if (present && now-last_ping >= 10000U) {
            if (!send_ping(fd,ping++,&next)) break;
            last_ping=now;
        }
        if (!resend(fd,&circuit,now) || !resend(fd,&movement,now)) {
            report("simulator reliable handshake acknowledgment timed out");
            break;
        }
    }
    close(fd);
    if (present) report("simulator circuit closed; avatar presence lost");
    return present;
}
void sparkles_sl_grid_run(atomic_int *stop)
{
    while (!atomic_load(stop)) {
        sparkles_sl_login_reply_t login={0};
        if (!sparkles_sl_login_from_file(&login)) {
            report("grid login failed (credentials, protocol, or network); no avatar presence");
        } else {
            report("grid login response accepted; attempting simulator circuit");
            (void)grid_session(&login,stop);
        }
        memset(&login,0,sizeof login);
        if (!atomic_load(stop)) pause_stop(stop,10000U);
    }
}
