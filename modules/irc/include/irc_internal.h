#ifndef SPARKLES_IRC_INTERNAL_H
#define SPARKLES_IRC_INTERNAL_H

#include <stddef.h>

#define SPARKLES_IRC_ENV_PATH "/opt/sparkles/auth/irc/irc.env"
#define SPARKLES_IRC_SERVER_MAX 256
#define SPARKLES_IRC_NICK_MAX 64
#define SPARKLES_IRC_CHANNEL_MAX 128
#define SPARKLES_IRC_ACCOUNT_MAX 128
#define SPARKLES_IRC_PASSWORD_MAX 256
#define SPARKLES_IRC_LINE_MAX 4096

typedef struct {
    char server[SPARKLES_IRC_SERVER_MAX];
    unsigned short port;
    char nick[SPARKLES_IRC_NICK_MAX];
    char channel[SPARKLES_IRC_CHANNEL_MAX];
    char account[SPARKLES_IRC_ACCOUNT_MAX];
    char password[SPARKLES_IRC_PASSWORD_MAX];
} sparkles_irc_config_t;

int sparkles_irc_config_load(const char *path, sparkles_irc_config_t *config);
void sparkles_irc_config_clear(sparkles_irc_config_t *config);

int sparkles_irc_line_command(const char *line, char *command, size_t command_size,
                            const char **params_out);
int sparkles_irc_word_present(const char *text, const char *word);
int sparkles_irc_build_plain(const char *account, const char *password,
                           char *output, size_t output_size);
int sparkles_irc_parse_privmsg(const char *line,
                             char *sender, size_t sender_size,
                             char *target, size_t target_size,
                             char *message, size_t message_size);
int sparkles_irc_run_selftests(unsigned int *executed, unsigned int *passed,
                             int *negative_executed, int *negative_passed);

#endif
