#include "sl_wire.h"
#include <string.h>

static int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

int sparkles_sl_uuid_parse(const char *text, unsigned char uuid[16])
{
    unsigned int i, digit = 0U;
    unsigned char scratch[16] = {0};
    if (!text || !uuid || strlen(text) != 36U) return 0;
    for (i = 0; i < 36U; ++i) {
        int v;
        if (i == 8U || i == 13U || i == 18U || i == 23U) {
            if (text[i] != '-') return 0;
            continue;
        }
        v = hex_digit((unsigned char)text[i]);
        if (v < 0) return 0;
        if ((digit & 1U) == 0U)
            scratch[digit / 2U] = (unsigned char)(v << 4);
        else
            scratch[digit / 2U] |= (unsigned char)v;
        ++digit;
    }
    memcpy(uuid, scratch, sizeof scratch);
    return 1;
}

void sparkles_sl_uuid_format(const unsigned char uuid[16], char output[37])
{
    static const char hex[] = "0123456789abcdef";
    unsigned int i, j = 0U;
    if (!uuid || !output) return;
    for (i = 0U; i < 16U; ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) output[j++] = '-';
        output[j++] = hex[(uuid[i] >> 4) & 15U];
        output[j++] = hex[uuid[i] & 15U];
    }
    output[j] = '\0';
}

static void put_u32be(unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char)(value >> 24);
    dst[1] = (unsigned char)(value >> 16);
    dst[2] = (unsigned char)(value >> 8);
    dst[3] = (unsigned char)value;
}
static void put_u32le(unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char)value;
    dst[1] = (unsigned char)(value >> 8);
    dst[2] = (unsigned char)(value >> 16);
    dst[3] = (unsigned char)(value >> 24);
}
static int write_handshake(const sparkles_sl_session_t *session,
                           uint32_t sequence, int movement,
                           unsigned char *out, size_t capacity,
                           size_t *used)
{
    if (used) *used = 0U;
    if (!session || !out || !used || capacity < 46U || sequence == 0U)
        return 0;
    memset(out, 0, 46U);
    out[0] = SPARKLES_SL_FLAG_RELIABLE;
    put_u32be(out + 1U, sequence);
    /* out[5] is extra header byte count. Low-frequency message prefix. */
    out[6] = 0xffU;
    out[7] = 0xffU;
    out[8] = 0x00U;
    out[9] = movement ? 0xf9U : 0x03U;
    if (!movement) {
        put_u32le(out + 10U, session->circuit);
        memcpy(out + 14U, session->session, 16U);
        memcpy(out + 30U, session->agent, 16U);
    } else {
        memcpy(out + 10U, session->agent, 16U);
        memcpy(out + 26U, session->session, 16U);
        put_u32le(out + 42U, session->circuit);
    }
    *used = 46U;
    return 1;
}
int sparkles_sl_use_circuit(const sparkles_sl_session_t *session,
                            uint32_t sequence, unsigned char *out,
                            size_t capacity, size_t *used)
{
    return write_handshake(session, sequence, 0, out, capacity, used);
}
int sparkles_sl_complete_movement(const sparkles_sl_session_t *session,
                                  uint32_t sequence, unsigned char *out,
                                  size_t capacity, size_t *used)
{
    return write_handshake(session, sequence, 1, out, capacity, used);
}

/* Zero coding covers the protocol body (message ID and payload) only,
 * not the first six bytes of the UDP header. Input is not trusted. */
int sparkles_sl_packet_view(const unsigned char *packet, size_t length,
                            unsigned char *decoded, size_t capacity,
                            sparkles_sl_packet_view_t *view)
{
    size_t src, dst, offset, end, message_start;
    unsigned char flags, frequency;
    if (!packet || !decoded || !view || length < 7U || capacity < 7U ||
        length > SPARKLES_SL_PACKET_MAX || capacity > SPARKLES_SL_PACKET_MAX)
        return 0;
    memset(view, 0, sizeof *view);
    flags = packet[0];
    offset = 6U + (size_t)packet[5];
    if (offset >= length || offset > capacity) return 0;
    /* Sequence can be accompanied by trailing ACK records. Strip them
     * before zerocode expansion or message decoding. */
    end = length;
    if (flags & 0x10U) {
        unsigned int ack_count;
        if (end < 8U) return 0;
        ack_count = packet[end - 1U];
        if (ack_count == 0U || (size_t)ack_count > (end - offset - 1U) / 4U)
            return 0;
        end -= 1U + (size_t)ack_count * 4U;
        if (end <= offset) return 0;
    }
    memcpy(decoded, packet, offset);
    dst = offset;
    for (src = offset; src < end; ++src) {
        if ((flags & SPARKLES_SL_FLAG_ZEROCODED) && packet[src] == 0U) {
            unsigned int count;
            if (++src >= end) return 0;
            count = packet[src];
            if (!count || count > capacity - dst) return 0;
            memset(decoded + dst, 0, count);
            dst += count;
        } else {
            if (dst >= capacity) return 0;
            decoded[dst++] = packet[src];
        }
    }
    message_start = offset;
    if (decoded[message_start] != 0xffU) {
        frequency = 0U;
        view->message_id = decoded[message_start++];
    } else {
        if (dst - message_start < 2U) return 0;
        if (decoded[message_start + 1U] != 0xffU) {
            frequency = 1U;
            view->message_id = decoded[message_start + 1U];
            message_start += 2U;
        } else {
            if (dst - message_start < 4U) return 0;
            frequency = 2U;
            view->message_id = (uint32_t)decoded[message_start + 2U] << 8U |
                               (uint32_t)decoded[message_start + 3U];
            message_start += 4U;
        }
    }
    view->flags = flags;
    view->sequence = ((uint32_t)packet[1] << 24U) |
                     ((uint32_t)packet[2] << 16U) |
                     ((uint32_t)packet[3] << 8U) | packet[4];
    view->message_frequency = frequency;
    view->payload = decoded + message_start;
    view->payload_size = dst - message_start;
    return 1;
}
