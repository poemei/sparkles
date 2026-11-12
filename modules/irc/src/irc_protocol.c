#include <ctype.h>
#include <string.h>
#include <openssl/evp.h>
#include "irc_internal.h"

static int ascii_equal_nocase(const char *left, const char *right, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (tolower(a) != tolower(b)) return 0;
    }
    return 1;
}

int sparkles_irc_line_command(const char *line, char *command, size_t command_size,
                            const char **params_out)
{
    const char *p;
    const char *start;
    size_t length;

    if (!line || !command || command_size < 2U || !params_out) return 0;
    p = line;
    if (*p == '@') {
        p = strchr(p, ' ');
        if (!p) return 0;
        while (*p == ' ') ++p;
    }
    if (*p == ':') {
        p = strchr(p, ' ');
        if (!p) return 0;
        while (*p == ' ') ++p;
    }
    if (!*p) return 0;
    start = p;
    while (*p && *p != ' ') ++p;
    length = (size_t)(p - start);
    if (!length || length >= command_size) return 0;
    memcpy(command, start, length);
    command[length] = '\0';
    while (*p == ' ') ++p;
    *params_out = p;
    return 1;
}

int sparkles_irc_word_present(const char *text, const char *word)
{
    size_t word_length;
    const char *p;

    if (!text || !word || !*word) return 0;
    word_length = strlen(word);
    for (p = text; *p; ++p) {
        int left_ok = (p == text) || isspace((unsigned char)p[-1]) || p[-1] == ':';
        int right_ok;
        size_t remaining;
        if (!left_ok) continue;
        remaining = strlen(p);
        if (remaining < word_length) return 0;
        if (!ascii_equal_nocase(p, word, word_length)) continue;
        right_ok = p[word_length] == '\0' ||
                   isspace((unsigned char)p[word_length]) ||
                   p[word_length] == '=';
        if (right_ok) return 1;
    }
    return 0;
}


/* Transport-level privacy gate. It does not infer or create appointments:
 * it only prevents scheduling requests from reaching Core via a channel. */
static int schedule_word(const char *text,const char *word)
{
    const char *p=text;
    size_t length=strlen(word);
    if(!text||!word||!length)return 0;
    while(*p) {
        const char *start;
        size_t n;
        while(*p && !(isalnum((unsigned char)*p)))++p;
        if(!*p)break;
        start=p;
        while(*p && (isalnum((unsigned char)*p)||*p=='_'||*p=='-'))++p;
        n=(size_t)(p-start);
        if(n==length && ascii_equal_nocase(start,word,length))return 1;
    }
    return 0;
}
static int schedule_phrase(const char *text,const char *phrase)
{
    size_t n=strlen(phrase);
    const char *p;
    for(p=text;*p;++p)
        if(strlen(p)>=n && ascii_equal_nocase(p,phrase,n))return 1;
    return 0;
}
static int schedule_time_token(const char *text)
{
    const char *p=text;
    while(p&&*p) {
        const char *q=p;
        size_t digits=0;
        if(p!=text && isalnum((unsigned char)p[-1])) {++p;continue;}
        while(digits<2U && isdigit((unsigned char)*q)) {++q;++digits;}
        if(digits && *q==':') {
            ++q;
            if(isdigit((unsigned char)q[0]) &&
               isdigit((unsigned char)q[1])) {
                q+=2;
                if(!isalnum((unsigned char)*q) ||
                   ((q[0]=='a'||q[0]=='A'||q[0]=='p'||q[0]=='P') &&
                    (q[1]=='m'||q[1]=='M') &&
                    !isalnum((unsigned char)q[2])))return 1;
            }
        }
        if(digits && (q[0]=='a'||q[0]=='A'||q[0]=='p'||q[0]=='P') &&
           (q[1]=='m'||q[1]=='M') && !isalnum((unsigned char)q[2]))
            return 1;
        ++p;
    }
    return 0;
}

int sparkles_irc_schedule_intent(const char *text)
{
    int scheduling,action,lookup;
    if(!text||!*text)return 0;
    while(*text==' '||*text=='\t')++text;
    /* Explicit routed commands never execute in a channel. */
    if(schedule_phrase(text,"/schedule")||
       schedule_phrase(text,"/appointment-add")||
       schedule_phrase(text,"/appointment-")||
       schedule_phrase(text,"/calendar"))return 1;
    scheduling=schedule_word(text,"schedule")||
       schedule_word(text,"schdule")||
       schedule_word(text,"calendar")||
       schedule_word(text,"appointment")||
       schedule_word(text,"appointments")||
       schedule_word(text,"meeting")||
       schedule_word(text,"meetings")||
       schedule_word(text,"booking")||
       schedule_word(text,"reservation")||
       schedule_word(text,"reminder")||
       schedule_word(text,"reminders");
    lookup=schedule_phrase(text,"what do i have")||
       schedule_phrase(text,"what have i got")||
       schedule_phrase(text,"what am i doing tomorrow")||
       schedule_phrase(text,"what am i doing today")||
       schedule_phrase(text,"am i free")||
       schedule_phrase(text,"am i available");
    action=schedule_word(text,"add")||
       schedule_word(text,"set")||
       schedule_word(text,"book")||
       schedule_word(text,"create")||
       schedule_word(text,"change")||
       schedule_word(text,"move")||
       schedule_word(text,"reschedule")||
       schedule_word(text,"cancel")||
       schedule_word(text,"delete")||
       schedule_word(text,"remove")||
       schedule_word(text,"confirm")||
       schedule_word(text,"save")||
       schedule_word(text,"check")||
       schedule_word(text,"show")||
       schedule_word(text,"list")||
       schedule_word(text,"tell")||
       schedule_word(text,"have")||
       schedule_word(text,"got")||
       schedule_word(text,"tomorrow")||
       schedule_word(text,"tommorrow")||
       schedule_word(text,"today");
    if(scheduling &&
       (action||schedule_word(text,"my")||schedule_word(text,"an")||
        schedule_word(text,"a")||schedule_word(text,"next")||
        schedule_word(text,"upcoming")))return 1;
    if(lookup)return 1;
    /* No channel follow-up can confirm or change an IM-created proposal. */
    if(schedule_word(text,"confirm")||schedule_word(text,"confirmed")||
       schedule_word(text,"cancel")||
       (strlen(text)==3U && ascii_equal_nocase(text,"yes",3U)) ||
       (strlen(text)==2U && ascii_equal_nocase(text,"no",2U)))
        return 1;
    if(schedule_time_token(text))return 1;
    return 0;
}


int sparkles_irc_parse_account_tag(const char *line, char *account,
                                  size_t account_size)
{
    const char *p, *end;
    size_t n, i;
    if (!line || !account || account_size < 2U) return 0;
    account[0] = '\0';
    if (*line != '@') return 0;
    end = strchr(line, ' ');
    if (!end) return 0;
    p = line + 1;
    while (p < end) {
        const char *item_end = memchr(p, ';', (size_t)(end - p));
        const char *value;
        if (!item_end) item_end = end;
        if ((size_t)(item_end - p) > 8U && !memcmp(p, "account=", 8U)) {
            value = p + 8;
            n = (size_t)(item_end - value);
            if (!n || n >= account_size || (n == 1U && *value == '*'))
                return 0;
            for (i = 0; i < n; ++i) {
                char c = value[i];
                if (!((c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.'))
                    return 0;
            }
            memcpy(account,value,n);
            account[n] = '\0';
            return 1;
        }
        p = item_end < end ? item_end + 1 : end;
    }
    return 0;
}

int sparkles_irc_parse_privmsg(const char *line,
                             char *sender, size_t sender_size,
                             char *target, size_t target_size,
                             char *message, size_t message_size)
{
    const char *p, *end, *text;
    size_t n;
    if (!line || !sender || sender_size < 2U ||
        !target || target_size < 2U || !message || message_size < 2U)
        return 0;
    p = line;
    /* IRCv3 message tags are optional. */
    if (*p == '@') {
        p = strchr(p, ' ');
        if (!p) return 0;
        while (*p == ' ') ++p;
    }
    if (*p++ != ':') return 0;
    end = strchr(p, ' ');
    if (!end) return 0;
    /* A nick prefix may be nick, nick!user or nick!user@host. */
    {
        const char *bang = memchr(p, '!', (size_t)(end - p));
        const char *at = memchr(p, '@', (size_t)(end - p));
        const char *nick_end = bang ? bang : (at ? at : end);
        n = (size_t)(nick_end - p);
    }
    if (!n || n >= sender_size) return 0;
    memcpy(sender, p, n);
    sender[n] = '\0';
    p = end;
    while (*p == ' ') ++p;
    if (strncmp(p, "PRIVMSG", 7) || p[7] != ' ') return 0;
    p += 7;
    while (*p == ' ') ++p;
    end = strchr(p, ' ');
    if (!end) return 0;
    n = (size_t)(end - p);
    if (!n || n >= target_size) return 0;
    memcpy(target, p, n);
    target[n] = '\0';
    p = end;
    while (*p == ' ') ++p;
    if (*p != ':') return 0;
    text = p + 1;
    n = strlen(text);
    if (!n || n >= message_size) return 0;
    memcpy(message, text, n + 1U);
    return 1;
}

int sparkles_irc_build_plain(const char *account, const char *password,
                           char *output, size_t output_size)
{
    unsigned char plain[SPARKLES_IRC_ACCOUNT_MAX + SPARKLES_IRC_PASSWORD_MAX + 2U];
    size_t account_length;
    size_t password_length;
    size_t plain_length;
    size_t encoded_length;
    int written;

    if (!account || !*account || !password || !*password || !output || !output_size)
        return 0;
    account_length = strlen(account);
    password_length = strlen(password);
    if (account_length >= SPARKLES_IRC_ACCOUNT_MAX ||
        password_length >= SPARKLES_IRC_PASSWORD_MAX) return 0;

    plain[0] = '\0';
    memcpy(plain + 1U, account, account_length);
    plain[1U + account_length] = '\0';
    memcpy(plain + 2U + account_length, password, password_length);
    plain_length = account_length + password_length + 2U;
    encoded_length = 4U * ((plain_length + 2U) / 3U);
    if (encoded_length + 1U > output_size) return 0;
    written = EVP_EncodeBlock((unsigned char *)output, plain, (int)plain_length);
    if (written < 0 || (size_t)written != encoded_length) return 0;
    output[encoded_length] = '\0';
    return 1;
}

static int expect(int condition, unsigned int *executed, unsigned int *passed)
{
    ++*executed;
    if (condition) {
        ++*passed;
        return 1;
    }
    return 0;
}

int sparkles_irc_run_selftests(unsigned int *executed, unsigned int *passed,
                             int *negative_executed, int *negative_passed)
{
    char command[32];
    const char *params = NULL;
    char encoded[256];
    char sender[64], target[128], message[256], account[128];
    unsigned int e = 0U;
    unsigned int p = 0U;
    int neg_e = 0;
    int neg_p = 0;

    expect(sparkles_irc_line_command("PING :abc", command, sizeof command, &params) &&
           !strcmp(command, "PING") && !strcmp(params, ":abc"), &e, &p);
    expect(sparkles_irc_line_command(":server 903 sparkles :SASL successful",
           command, sizeof command, &params) && !strcmp(command, "903"), &e, &p);
    expect(sparkles_irc_line_command(":server CAP sparkles LS :multi-prefix sasl",
           command, sizeof command, &params) && !strcmp(command, "CAP"), &e, &p);
    expect(sparkles_irc_word_present("multi-prefix sasl account-tag", "sasl"), &e, &p);
    expect(sparkles_irc_word_present("SASL", "sasl"), &e, &p);
    expect(!sparkles_irc_word_present("nosaslhere", "sasl"), &e, &p);
    expect(sparkles_irc_build_plain("u", "p", encoded, sizeof encoded) &&
           !strcmp(encoded, "AHUAcA=="), &e, &p);
    expect(!sparkles_irc_build_plain("", "p", encoded, sizeof encoded), &e, &p);
    expect(!sparkles_irc_build_plain("u", "", encoded, sizeof encoded), &e, &p);
    expect(!sparkles_irc_line_command("", command, sizeof command, &params), &e, &p);
    expect(sparkles_irc_line_command(":s NOTICE r :hello",
           command, sizeof command, &params) && !strcmp(command, "NOTICE"), &e, &p);
    expect(sparkles_irc_word_present("sasl=PLAIN,EXTERNAL", "sasl"), &e, &p);
    expect(sparkles_irc_parse_privmsg(":PoeMei!u@h PRIVMSG #stn-labz :sparkles: status", sender, sizeof sender, target, sizeof target, message, sizeof message) && !strcmp(sender, "PoeMei") && !strcmp(target, "#stn-labz") && !strcmp(message, "sparkles: status"), &e, &p);
    expect(sparkles_irc_parse_privmsg(":PoeMei!u@h PRIVMSG sparkles :hello", sender, sizeof sender, target, sizeof target, message, sizeof message) && !strcmp(target, "sparkles") && !strcmp(message, "hello"), &e, &p);
    expect(sparkles_irc_line_command("@account=PoeMei :PoeMei!u@h PRIVMSG #stn-labz :sparkles: who are you?", command, sizeof command, &params) && !strcmp(command, "PRIVMSG"), &e, &p);
    expect(sparkles_irc_parse_privmsg("@account=PoeMei :PoeMei!u@h PRIVMSG #stn-labz :sparkles: who are you?", sender, sizeof sender, target, sizeof target, message, sizeof message) && !strcmp(sender, "PoeMei") && !strcmp(target, "#stn-labz") && !strcmp(message, "sparkles: who are you?"), &e, &p);

    expect(sparkles_irc_parse_privmsg(
           ":PoeMei PRIVMSG #stn-labz :sparkles: hello",
           sender, sizeof sender, target, sizeof target, message, sizeof message) &&
           !strcmp(sender, "PoeMei") && !strcmp(target, "#stn-labz") &&
           !strcmp(message, "sparkles: hello"), &e, &p);
    expect(sparkles_irc_parse_privmsg(
           "@account=PoeMei :PoeMei!u@h PRIVMSG #second :sparkles: online?",
           sender, sizeof sender, target, sizeof target, message, sizeof message) &&
           !strcmp(sender, "PoeMei") && !strcmp(target, "#second") &&
           !strcmp(message, "sparkles: online?"), &e, &p);
    expect(!sparkles_irc_parse_privmsg(
           ":PoeMei NOTICE #second :sparkles: ignored",
           sender, sizeof sender, target, sizeof target, message, sizeof message), &e, &p);
    expect(!sparkles_irc_parse_privmsg(
           ":PoeMei PRIVMSG #second",
           sender, sizeof sender, target, sizeof target, message, sizeof message), &e, &p);

    expect(sparkles_irc_parse_account_tag("@time=1;account=PoeMei :PoeMei!u@h PRIVMSG #test :hi",account,sizeof account) && !strcmp(account,"PoeMei"),&e,&p);
    expect(!sparkles_irc_parse_account_tag(":PoeMei!u@h PRIVMSG #test :hi",account,sizeof account),&e,&p);
    expect(!sparkles_irc_parse_account_tag("@account=* :PoeMei!u@h PRIVMSG #test :hi",account,sizeof account),&e,&p);
    expect(!sparkles_irc_parse_account_tag("@account=Poe\\:Mei :x PRIVMSG #test :hi",account,sizeof account),&e,&p);
    expect(!sparkles_irc_parse_account_tag("@account= :x PRIVMSG #test :hi",account,sizeof account),&e,&p);
    expect(!sparkles_irc_parse_account_tag("@time=1 :x PRIVMSG #test :hi",account,sizeof account),&e,&p);
    expect(!sparkles_irc_parse_account_tag("@account=PoeMei :x PRIVMSG #test :hi",account,3U),&e,&p);

    expect(sparkles_irc_schedule_intent("I have an appointment tomorrow 11/12/2026 at River at 4:30pm"),&e,&p);
    expect(sparkles_irc_schedule_intent("what is my schdule for tommorrow?"),&e,&p);
    expect(sparkles_irc_schedule_intent("/schedule 2026-11-12 2026-11-13"),&e,&p);
    expect(sparkles_irc_schedule_intent("/appointment-add 2026-11-12 16:30 17:00 River"),&e,&p);
    expect(sparkles_irc_schedule_intent("move my meeting to next week"),&e,&p);
    expect(sparkles_irc_schedule_intent("cancel my appointment"),&e,&p);
    expect(sparkles_irc_schedule_intent("remind me about my appointment"),&e,&p);
    expect(sparkles_irc_schedule_intent("what do I have tomorrow?"),&e,&p);
    expect(sparkles_irc_schedule_intent("confirm"),&e,&p);
    expect(sparkles_irc_schedule_intent("17:00"),&e,&p);
    expect(sparkles_irc_schedule_intent("yes"),&e,&p);
    expect(sparkles_irc_schedule_intent("no"),&e,&p);
    expect(sparkles_irc_schedule_intent("4:30pm"),&e,&p);
    expect(sparkles_irc_schedule_intent("5pm"),&e,&p);
    expect(sparkles_irc_schedule_intent("at 4:30pm with management"),&e,&p);
    expect(!sparkles_irc_schedule_intent("hello sparkles"),&e,&p);
    expect(!sparkles_irc_schedule_intent("how is the weather?"),&e,&p);
    expect(!sparkles_irc_schedule_intent("tell digicat hello"),&e,&p);
    expect(!sparkles_irc_schedule_intent("I like the show at the River"),&e,&p);
    expect(!sparkles_irc_schedule_intent("how does calendar software work?"),&e,&p);

    neg_e = 1;
    neg_p = !sparkles_irc_build_plain(NULL, "p", encoded, sizeof encoded);

    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = neg_e;
    if (negative_passed) *negative_passed = neg_p;
    return e >= 10U && e == p && neg_e && neg_p;
}
