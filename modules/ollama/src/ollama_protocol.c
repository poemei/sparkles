#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ollama_internal.h"

static int append_char(char *output, size_t output_size, size_t *used, char c)
{
    if (!output || !used || *used + 1U >= output_size) return 0;
    output[(*used)++] = c;
    output[*used] = '\0';
    return 1;
}

static int append_text(char *output, size_t output_size, size_t *used,
                       const char *text)
{
    while (text && *text) {
        if (!append_char(output, output_size, used, *text++)) return 0;
    }
    return 1;
}

static int append_json_string(char *output, size_t output_size, size_t *used,
                              const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!text) return 0;
    while (*p) {
        char escape[7];
        if (*p == '"' || *p == '\\') {
            if (!append_char(output, output_size, used, '\\') ||
                !append_char(output, output_size, used, (char)*p))
                return 0;
        } else if (*p == '\n') {
            if (!append_text(output, output_size, used, "\\n")) return 0;
        } else if (*p == '\r') {
            if (!append_text(output, output_size, used, "\\r")) return 0;
        } else if (*p == '\t') {
            if (!append_text(output, output_size, used, "\\t")) return 0;
        } else if (*p < 0x20U) {
            if (snprintf(escape, sizeof escape, "\\u%04x", (unsigned)*p) != 6)
                return 0;
            if (!append_text(output, output_size, used, escape)) return 0;
        } else {
            if (!append_char(output, output_size, used, (char)*p)) return 0;
        }
        ++p;
    }
    return 1;
}

int sparkles_ollama_build_request(const char *prompt,
                                char *output, size_t output_size)
{
    size_t used = 0U;
    const char *prefix =
        "{\"model\":\"llama3.2:1b\",\"stream\":false,"
        "\"system\":\"You are Sparkles, the STN-LABZ Control Automated Agent. "
        "Your identity is Sparkles. Digit is a separate STN-LABZ terrestrial AI agent. "
        "Never identify yourself as Digit or speak as Digit. "
        "When the operator asks you to say, tell, ask, or otherwise speak to an IRC participant, "
        "address that participant directly using their exact IRC nickname followed by a colon, "
        "then the message. Preserve the operator's intent: say/tell relays a statement, while ask "
        "must produce a question for that participant and must not answer on their behalf. "
        "For example, say hi to digicat should begin with digicat: hi, while ask digicat how she is "
        "should produce digicat: how are you?. Do not narrate that you will send the message; address them. "
        "Answer concisely and directly. Do not invent operational facts.\","
        "\"prompt\":\"";
    const char *suffix =
        "\",\"options\":{\"temperature\":0.4,\"num_predict\":96}}";

    if (!prompt || !*prompt || !output || output_size < 2U) return 0;
    output[0] = '\0';
    if (!append_text(output, output_size, &used, prefix) ||
        !append_json_string(output, output_size, &used, prompt) ||
        !append_text(output, output_size, &used, suffix))
        return 0;
    return 1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int append_codepoint(char *output, size_t output_size, size_t *used,
                            unsigned int codepoint)
{
    if (codepoint <= 0x7fU)
        return append_char(output, output_size, used, (char)codepoint);
    if (codepoint <= 0x7ffU) {
        return append_char(output, output_size, used,
                           (char)(0xc0U | (codepoint >> 6))) &&
               append_char(output, output_size, used,
                           (char)(0x80U | (codepoint & 0x3fU)));
    }
    if (codepoint >= 0xd800U && codepoint <= 0xdfffU) return 0;
    return append_char(output, output_size, used,
                       (char)(0xe0U | (codepoint >> 12))) &&
           append_char(output, output_size, used,
                       (char)(0x80U | ((codepoint >> 6) & 0x3fU))) &&
           append_char(output, output_size, used,
                       (char)(0x80U | (codepoint & 0x3fU)));
}

int sparkles_ollama_extract_response(const char *json,
                                   char *output, size_t output_size)
{
    const char *p;
    size_t used = 0U;

    if (!json || !output || output_size < 2U) return 0;
    output[0] = '\0';
    p = strstr(json, "\"response\":");
    if (!p) return 0;
    p += strlen("\"response\":");
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p++ != '"') return 0;

    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (c == '"') return used > 0U;
        if (c == '\\') {
            int h0, h1, h2, h3;
            unsigned int cp;
            c = (unsigned char)*p++;
            if (!c) return 0;
            if (c == '"' || c == '\\' || c == '/')
                ;
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {
                h0 = hex_value(p[0]); h1 = hex_value(p[1]);
                h2 = hex_value(p[2]); h3 = hex_value(p[3]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return 0;
                cp = ((unsigned)h0 << 12) | ((unsigned)h1 << 8) |
                     ((unsigned)h2 << 4) | (unsigned)h3;
                p += 4;
                if (!append_codepoint(output, output_size, &used, cp)) return 0;
                continue;
            } else return 0;
        }
        if (c < 0x20U && c != '\n' && c != '\r' && c != '\t') return 0;
        if (!append_char(output, output_size, &used, (char)c)) return 0;
    }
    return 0;
}

static int expect(int condition, unsigned int *executed, unsigned int *passed)
{
    ++*executed;
    if (condition) ++*passed;
    return condition;
}

int sparkles_ollama_run_selftests(unsigned int *executed,
                                unsigned int *passed,
                                int *negative_executed,
                                int *negative_passed)
{
    char request[4096];
    char response[512];
    unsigned int e = 0U, p = 0U;
    int ne = 0, np = 0;

    expect(sparkles_ollama_build_request("hello", request, sizeof request) &&
           strstr(request, "\"model\":\"llama3.2:1b\"") &&
           strstr(request, "\"prompt\":\"hello\""), &e, &p);
    expect(sparkles_ollama_build_request("say \"hi\"", request, sizeof request) &&
           strstr(request, "say \\\"hi\\\""), &e, &p);
    expect(sparkles_ollama_build_request("identity", request, sizeof request) &&
           strstr(request, "Your identity is Sparkles.") &&
           strstr(request, "Digit is a separate STN-LABZ terrestrial AI agent.") &&
           strstr(request, "Never identify yourself as Digit or speak as Digit."),
           &e, &p);
    expect(sparkles_ollama_build_request("say hi to digicat", request, sizeof request) &&
           strstr(request, "exact IRC nickname followed by a colon") &&
           strstr(request, "say/tell relays a statement") &&
           strstr(request, "ask must produce a question") &&
           strstr(request, "must not answer on their behalf") &&
           strstr(request, "Do not narrate that you will send the message"),
           &e, &p);
    expect(sparkles_ollama_extract_response(
           "{\"response\":\"Hello there\",\"done\":true}",
           response, sizeof response) && !strcmp(response, "Hello there"), &e, &p);
    expect(sparkles_ollama_extract_response(
           "{\"response\":\"line1\\nline2\"}",
           response, sizeof response) && !strcmp(response, "line1\nline2"), &e, &p);
    expect(sparkles_ollama_extract_response(
           "{\"response\":\"caf\\u00e9\"}",
           response, sizeof response) && !strcmp(response, "caf\xc3\xa9"), &e, &p);
    expect(!sparkles_ollama_extract_response("{\"done\":true}",
           response, sizeof response), &e, &p);
    expect(!sparkles_ollama_build_request("", request, sizeof request), &e, &p);
    expect(sparkles_ollama_build_request("line1\nline2", request, sizeof request) &&
           strstr(request, "line1\\nline2"), &e, &p);
    expect(sparkles_ollama_build_request("back\\slash", request, sizeof request) &&
           strstr(request, "back\\\\slash"), &e, &p);
    expect(!sparkles_ollama_extract_response(
           "{\"response\":\"\"}", response, sizeof response), &e, &p);

    ne = 1;
    np = !sparkles_ollama_extract_response(NULL, response, sizeof response);

    if (executed) *executed = e;
    if (passed) *passed = p;
    if (negative_executed) *negative_executed = ne;
    if (negative_passed) *negative_passed = np;
    return e == p && e >= 10U && ne && np;
}
