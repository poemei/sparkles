#ifndef SPARKLES_TRANSPORT_H
#define SPARKLES_TRANSPORT_H

#define SPARKLES_TRANSPORT_CHANNEL_SEND_SERVICE "digit.transport.channel.send"
#define SPARKLES_TRANSPORT_PRIVATE_SEND_SERVICE "digit.transport.private.send"

#define SPARKLES_TRANSPORT_TARGET_MAX 128
#define SPARKLES_TRANSPORT_MESSAGE_MAX 512

typedef struct {
    char message[SPARKLES_TRANSPORT_MESSAGE_MAX];
} sparkles_transport_channel_request_t;

typedef struct {
    char target[SPARKLES_TRANSPORT_TARGET_MAX];
    char message[SPARKLES_TRANSPORT_MESSAGE_MAX];
} sparkles_transport_private_request_t;

#endif
