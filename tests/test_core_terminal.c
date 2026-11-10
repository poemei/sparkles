#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../src/runtime.c"

static unsigned executed, passed, dispatched;
static char last_input[SPARKLES_ROUTE_TEXT_MAX];

#define CHECK(expr) do { \
    int valid = !!(expr); \
    ++executed; \
    if (valid) ++passed; \
    else fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
} while (0)

static int mock_dispatch(void *context, const char *message,
                         char *response, size_t capacity)
{
    (void)context;
    ++dispatched;
    snprintf(last_input, sizeof last_input, "%s", message);
    if (capacity < sizeof "Acknowledged") return 0;
    strcpy(response, "Acknowledged");
    return 1;
}

int main(void)
{
    stnlabz_runtime_t runtime;
    sparkles_terminal_t terminal = {0};
    int fd[2] = {-1,-1};
    int saved_stdin;
    char large[SPARKLES_ROUTE_TEXT_MAX + 8];
    stnlabz_runtime_init(&runtime);
    CHECK(runtime.terminal_dispatch == NULL);
    runtime.terminal_dispatch = mock_dispatch;
    terminal.enabled = 1;
    CHECK(pipe(fd) == 0);
    saved_stdin = dup(STDIN_FILENO);
    CHECK(saved_stdin >= 0);
    if (fd[0] < 0 || saved_stdin < 0) return 2;
    CHECK(dup2(fd[0], STDIN_FILENO) == STDIN_FILENO);
    CHECK(write(fd[1], "Hello\n", 6) == 6);
    terminal_read(&runtime, &terminal);
    CHECK(dispatched == 1 && !strcmp(last_input, "Hello"));
    CHECK(terminal.used == 0 && !terminal.overflow);
    memset(large, 'x', sizeof large);
    large[sizeof large - 1] = '\n';
    CHECK(write(fd[1], large, sizeof large) == (ssize_t)sizeof large);
    while (!terminal.overflow) terminal_read(&runtime, &terminal);
    /* Drain the remaining oversized line and check that it is rejected. */
    while (terminal.overflow) terminal_read(&runtime, &terminal);
    CHECK(dispatched == 1);
    CHECK(terminal.used == 0);
    CHECK(write(fd[1], "Next\n", 5) == 5);
    terminal_read(&runtime, &terminal);
    CHECK(dispatched == 2 && !strcmp(last_input, "Next"));
    CHECK(write(fd[1], "quit\n", 5) == 5);
    terminal_read(&runtime, &terminal);
    CHECK(stnlabz_stop_requested == 1);
    CHECK(dispatched == 2);
    CHECK(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    close(saved_stdin);
    close(fd[0]);
    close(fd[1]);
    printf("Core terminal: %u/%u checks %s\n",
           passed, executed, passed == executed ? "PASS" : "FAIL");
    return passed == executed ? 0 : 1;
}
