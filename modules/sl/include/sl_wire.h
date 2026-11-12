#ifndef SPARKLES_SL_WIRE_H
#define SPARKLES_SL_WIRE_H
#include <stddef.h>
#include <stdint.h>

/* Second Life simulator UDP wire primitives. Pure ISO C11, no C++ or C#.
 * These functions construct packets only; they DO NOT establish presence,
 * authenticate grid identity, or deliver messages. */
#define SPARKLES_SL_UUID_BYTES 16U
#define SPARKLES_SL_PACKET_MAX 1200U
#define SPARKLES_SL_FLAG_RELIABLE 0x40U
#define SPARKLES_SL_FLAG_ZEROCODED 0x80U

typedef struct {
    unsigned char agent[SPARKLES_SL_UUID_BYTES];
    unsigned char session[SPARKLES_SL_UUID_BYTES];
    uint32_t circuit;
} sparkles_sl_session_t;

typedef struct {
    unsigned char flags;
    uint32_t sequence;
    unsigned char message_frequency;
    uint32_t message_id;
    const unsigned char *payload;
    size_t payload_size;
} sparkles_sl_packet_view_t;

int sparkles_sl_uuid_parse(const char *text, unsigned char uuid[16]);
void sparkles_sl_uuid_format(const unsigned char uuid[16], char output[37]);
int sparkles_sl_use_circuit(const sparkles_sl_session_t *session,
                           uint32_t sequence, unsigned char *out,
                           size_t capacity, size_t *used);
int sparkles_sl_complete_movement(const sparkles_sl_session_t *session,
                                 uint32_t sequence, unsigned char *out,
                                 size_t capacity, size_t *used);
int sparkles_sl_packet_view(const unsigned char *packet, size_t length,
                            unsigned char *decoded, size_t capacity,
                            sparkles_sl_packet_view_t *view);
#endif
