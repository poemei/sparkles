#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Include the parser implementation to exercise LLSD and session-state
 * behavior without contacting the Second Life grid or external services. */
#include "../src/sl_caps.c"

static sparkles_sl_private_im_t received;
static unsigned int calls;
static int sink(const sparkles_sl_private_im_t *im,void *unused)
{
    (void)unused;
    ++calls;
    received=*im;
    return 1;
}
int main(void)
{
    static const char event[] =
        "<llsd><map>"
        "<key>id</key><integer>41</integer>"
        "<key>events</key><array><map>"
        "<key>message</key><string>ChatterBoxInvitation</string>"
        "<key>body</key><map>"
        "<key>session_id</key><uuid>12345678-1234-1234-1234-123456789abc</uuid>"
        "<key>instantmessage</key><map><key>message_params</key><map>"
        "<key>type</key><integer>17</integer>"
        "<key>from_id</key><uuid>22345678-1234-1234-1234-123456789abc</uuid>"
        "<key>from_name</key><string>Poe</string>"
        "<key>message</key><string>Sparkles, are you here?</string>"
        "</map></map></map></map></array></map></llsd>";
    static const char empty[] =
        "<llsd><map><key>events</key><array/>"
        "<key>id</key><integer>42</integer></map></llsd>";
    static const char invalid[] =
        "<llsd><map><key>id</key><integer>abc</integer>"
        "<key>events</key><array/></map></llsd>";
    caps_state_t state={0};
    sparkles_sl_caps_worker_t worker={0};
    caps_body_t *response=calloc(1,sizeof *response);
    unsigned char session[16];
    atomic_int stop=ATOMIC_VAR_INIT(0);
    assert(response);
    worker.module_stop=&stop;
    worker.ingress=sink;
    worker.agent[0]=0x55U;
    atomic_init(&worker.session_stop,0);
    state.worker=&worker;
    assert(sparkles_sl_uuid_parse("12345678-1234-1234-1234-123456789abc",session));
    memcpy(worker.group_chat,session,16U);
    {
        static const char start_event[]=
          "<llsd><map><key>id</key><integer>40</integer>"
          "<key>events</key><array><map>"
          "<key>message</key><string>ChatterBoxSessionStartReply</string>"
          "<key>body</key><map>"
          "<key>session_id</key><uuid>12345678-1234-1234-1234-123456789abc</uuid>"
          "<key>success</key><boolean>1</boolean>"
          "</map></map></array></map></llsd>";
        memcpy(response->bytes,start_event,sizeof start_event);
        response->length=sizeof start_event-1U;
        assert(consume_events(&state,response));
        assert(joined(&state,session));
        assert(state.ack==40);
    }
    memcpy(response->bytes,event,sizeof event);
    response->length=sizeof event-1U;
    assert(consume_events(&state,response));
    assert(calls==1U && received.ambient==2U);
    assert(!strcmp(received.name,"Poe"));
    assert(!strcmp(received.text,"Sparkles, are you here?"));
    assert(!memcmp(received.conversation,session,16U));
    assert(state.has_ack && state.ack==41);
    memcpy(response->bytes,empty,sizeof empty);
    response->length=sizeof empty-1U;
    assert(consume_events(&state,response));
    assert(calls==1U && state.ack==42);
    memcpy(response->bytes,invalid,sizeof invalid);
    response->length=sizeof invalid-1U;
    assert(!consume_events(&state,response));
    assert(state.ack==42);
    {
        static const char close_event[]=
          "<llsd><map><key>id</key><integer>43</integer>"
          "<key>events</key><array><map>"
          "<key>message</key><string>ForceCloseChatterBoxSession</string>"
          "<key>body</key><map>"
          "<key>session_id</key><uuid>12345678-1234-1234-1234-123456789abc</uuid>"
          "</map></map></array></map></llsd>";
        memcpy(response->bytes,close_event,sizeof close_event);
        response->length=sizeof close_event-1U;
        assert(consume_events(&state,response));
        assert(!joined(&state,session));
    }
    assert(valid_cap_url("https://sim.example.com/cap/abc"));
    assert(!valid_cap_url("http://sim.example.com/cap/abc"));
    assert(!valid_cap_url("file:///etc/passwd"));
    free(response);
    puts("SL capability LLSD event and session ingress regression: PASS");
    return 0;
}
