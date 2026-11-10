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
    char sender[64], target[128], message[256];
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

    neg_e = 1;
    neg_p = !sparkles_irc_build_plain(NULL, "p", encoded, sizeof encoded);

    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = neg_e;
    if (negative_passed) *negative_passed = neg_p;
    return e >= 10U && e == p && neg_e && neg_p;
}
