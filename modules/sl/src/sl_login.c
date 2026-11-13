#define _POSIX_C_SOURCE 200809L
#include "sl_login.h"
#include "sl_config.h"
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The legacy XML-RPC login endpoint remains documented for third-party
 * clients, but its acceptance by the current grid must be operator-tested.
 * A contemporary authentication challenge is never bypassed. */
#define SL_LOGIN_URL "https://login.agni.lindenlab.com/cgi-bin/login.cgi"
#define SL_LOGIN_RESPONSE_MAX (1024U * 1024U)

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} response_buf_t;

static int password_digest(const char *secret, char output[36])
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0U;
    static const char hex[] = "0123456789abcdef";
    unsigned int i;
    if (!secret || !output ||
        !EVP_Digest(secret, strlen(secret), digest, &length, EVP_md5(), NULL) ||
        length != 16U) return 0;
    memcpy(output, "$1$", 3U);
    for (i = 0U; i < 16U; ++i) {
        output[i * 2U + 3U] = hex[digest[i] >> 4U];
        output[i * 2U + 4U] = hex[digest[i] & 15U];
    }
    output[35] = '\0';
    OPENSSL_cleanse(digest, sizeof digest);
    return 1;
}
static int element(xmlTextWriterPtr writer, const char *name, const char *value)
{
    return xmlTextWriterWriteElement(writer, BAD_CAST name, BAD_CAST value) >= 0;
}
static int member(xmlTextWriterPtr writer, const char *name,
                  const char *type, const char *value)
{
    return xmlTextWriterStartElement(writer, BAD_CAST "member") >= 0 &&
           element(writer, "name", name) &&
           xmlTextWriterStartElement(writer, BAD_CAST "value") >= 0 &&
           element(writer, type, value) &&
           xmlTextWriterEndElement(writer) >= 0 &&
           xmlTextWriterEndElement(writer) >= 0;
}
static int make_request(const sparkles_sl_login_config_t *config,
                        const char *passwd, xmlBufferPtr buffer)
{
    xmlTextWriterPtr w;
    int ok;
    w = xmlNewTextWriterMemory(buffer, 0);
    if (!w) return 0;
    ok = xmlTextWriterStartDocument(w, "1.0", "UTF-8", NULL) >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "methodCall") >= 0 &&
         element(w, "methodName", "login_to_simulator") &&
         xmlTextWriterStartElement(w, BAD_CAST "params") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "param") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "value") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "struct") >= 0 &&
         member(w, "first", "string", config->first) &&
         member(w, "last", "string", config->last) &&
         member(w, "passwd", "string", passwd) &&
         member(w, "start", "string", config->start) &&
         member(w, "channel", "string", "STN-Labz Sparkles") &&
         member(w, "version", "string", "0.2.0") &&
         member(w, "platform", "string", "Lin") &&
         member(w, "mac", "string", "00:00:00:00:00:00") &&
         member(w, "id0", "string", "00000000-0000-0000-0000-000000000000") &&
         member(w, "agree_to_tos", "boolean", "0") &&
         member(w, "read_critical", "boolean", "0") &&
         member(w, "viewer_digest", "string", "") &&
         xmlTextWriterStartElement(w, BAD_CAST "member") >= 0 &&
         element(w, "name", "options") &&
         xmlTextWriterStartElement(w, BAD_CAST "value") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "array") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "data") >= 0 &&
         xmlTextWriterStartElement(w, BAD_CAST "value") >= 0 &&
         element(w, "string", "adult_compliant") &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndElement(w) >= 0 &&
         xmlTextWriterEndDocument(w) >= 0;
    xmlFreeTextWriter(w);
    return ok;
}
static size_t write_response(char *data, size_t size, size_t count, void *ptr)
{
    response_buf_t *out = ptr;
    size_t bytes;
    unsigned char *resized;
    if (count && size > SIZE_MAX / count) return 0;
    bytes = size * count;
    if (bytes > SL_LOGIN_RESPONSE_MAX - out->length) return 0;
    if (!out->data || bytes > out->capacity - out->length - 1U) {
        size_t needed = out->length + bytes + 1U;
        resized = realloc(out->data, needed);
        if (!resized) return 0;
        out->data = resized;
        out->capacity = needed;
    }
    memcpy(out->data + out->length, data, bytes);
    out->length += bytes;
    out->data[out->length] = '\0';
    return bytes;
}
static xmlNodePtr child(xmlNodePtr parent, const char *name)
{
    xmlNodePtr node;
    for (node = parent ? parent->children : NULL; node; node = node->next)
        if (node->type == XML_ELEMENT_NODE &&
            !xmlStrcmp(node->name, BAD_CAST name)) return node;
    return NULL;
}
static xmlNodePtr get_struct(xmlDocPtr doc)
{
    xmlNodePtr root = xmlDocGetRootElement(doc);
    xmlNodePtr params = child(root, "params");
    xmlNodePtr param = child(params, "param");
    xmlNodePtr value = child(param, "value");
    if (!root || xmlStrcmp(root->name, BAD_CAST "methodResponse"))
        return NULL;
    return child(value, "struct");
}
static int get_field(xmlNodePtr structure, const char *field,
                     char *out, size_t cap)
{
    xmlNodePtr m;
    if (!structure || !out || !cap) return 0;
    for (m = structure->children; m; m = m->next) {
        xmlNodePtr key, value, value_node;
        xmlChar *name, *content;
        size_t len;
        if (m->type != XML_ELEMENT_NODE ||
            xmlStrcmp(m->name, BAD_CAST "member")) continue;
        key = child(m, "name");
        value = child(m, "value");
        if (!key || !value) continue;
        name = xmlNodeGetContent(key);
        if (!name) continue;
        if (strcmp((const char *)name, field)) {
            xmlFree(name);
            continue;
        }
        xmlFree(name);
        value_node = value->children;
        while (value_node && value_node->type != XML_ELEMENT_NODE)
            value_node = value_node->next;
        if (!value_node || (xmlStrcmp(value_node->name, BAD_CAST "string") &&
            xmlStrcmp(value_node->name, BAD_CAST "int") &&
            xmlStrcmp(value_node->name, BAD_CAST "i4") &&
            xmlStrcmp(value_node->name, BAD_CAST "boolean"))) return 0;
        content = xmlNodeGetContent(value_node);
        if (!content) return 0;
        len = strlen((const char *)content);
        if (!len || len >= cap) { xmlFree(content); return 0; }
        memcpy(out, content, len + 1U);
        xmlFree(content);
        return 1;
    }
    return 0;
}
static int parse_login(const unsigned char *buffer, size_t length,
                       sparkles_sl_login_reply_t *reply)
{
    xmlDocPtr doc;
    xmlNodePtr structure;
    char login[24], session[48], agent[48], port[16], circuit[24];
    char *end;
    unsigned long value;
    struct in_addr address;
    sparkles_sl_login_reply_t candidate = {0};
    int good = 0;
    doc = xmlReadMemory((const char *)buffer, (int)length, "sl_login.xml",
                        NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return 0;
    structure = get_struct(doc);
    if (!get_field(structure,"login",login,sizeof login) ||
        (strcmp(login,"true") && strcmp(login,"1")) ||
        !get_field(structure,"agent_id",agent,sizeof agent) ||
        !get_field(structure,"session_id",session,sizeof session) ||
        !get_field(structure,"sim_ip",candidate.sim_ip,sizeof candidate.sim_ip) ||
        !get_field(structure,"sim_port",port,sizeof port) ||
        !get_field(structure,"circuit_code",circuit,sizeof circuit) ||
        !get_field(structure,"seed_capability",candidate.seed_capability,
                   sizeof candidate.seed_capability) ||
        !sparkles_sl_uuid_parse(agent,candidate.circuit.agent) ||
        !sparkles_sl_uuid_parse(session,candidate.circuit.session) ||
        inet_pton(AF_INET,candidate.sim_ip,&address)!=1)
        goto done;
    errno = 0;
    value = strtoul(port,&end,10);
    if (errno || *end || value < 1UL || value > 65535UL) goto done;
    candidate.sim_port=(unsigned short)value;
    errno = 0;
    value = strtoul(circuit,&end,10);
    if (errno || *end || value > UINT32_MAX) goto done;
    candidate.circuit.circuit=(uint32_t)value;
    if (strncmp(candidate.seed_capability,"https://",8U)) goto done;
    *reply=candidate;
    good=1;
done:
    xmlFreeDoc(doc);
    return good;
}
int sparkles_sl_login(const sparkles_sl_login_config_t *config,
                     sparkles_sl_login_reply_t *reply)
{
    char secret[256] = {0}, hashed[36] = {0};
    xmlBufferPtr request = NULL;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    CURLcode code;
    long status = 0;
    response_buf_t response = {0};
    int ok = 0;
    if (!config || !reply || !config->first[0] || !config->last[0] ||
        !config->password[0] || !config->start[0]) {
        fprintf(stderr,"[SL] login request rejected: missing required configuration fields\n");
        return 0;
    }
    memset(reply,0,sizeof *reply);
    if (strlen(config->password) >= sizeof secret) {
        fprintf(stderr,"[SL] login request rejected: password exceeds limit\n");
        goto done;
    }
    memcpy(secret, config->password, strlen(config->password) + 1U);
    if (!password_digest(secret,hashed)) {
        fprintf(stderr,"[SL] login request failed: password digest unavailable\n");
        goto done;
    }
    request=xmlBufferCreate();
    if (!request || !make_request(config,hashed,request)) {
        fprintf(stderr,"[SL] login request failed: XML-RPC serialization\n");
        goto done;
    }
    curl=curl_easy_init();
    if (!curl) {
        fprintf(stderr,"[SL] login request failed: HTTP client initialization\n");
        goto done;
    }
    headers=curl_slist_append(NULL,"Content-Type: text/xml");
    if (!headers) {
        fprintf(stderr,"[SL] login request failed: HTTP header initialization\n");
        goto done;
    }
    if (curl_easy_setopt(curl,CURLOPT_URL,SL_LOGIN_URL)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_POST,1L)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,request->content)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)request->use)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_response)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_TIMEOUT,30L)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L)!=CURLE_OK ||
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L)!=CURLE_OK)
    {
        fprintf(stderr,"[SL] login request failed: HTTP client configuration\n");
        goto done;
    }
    code=curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr,"[SL] login transport failed: curl_code=%d\n",(int)code);
        goto done;
    }
    if (curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status)!=CURLE_OK) {
        fprintf(stderr,"[SL] login transport failed: no HTTP status\n");
        goto done;
    }
    if (status != 200L) {
        fprintf(stderr,"[SL] login HTTP rejected: status=%ld\n",status);
        goto done;
    }
    if (!response.data || !response.length) {
        fprintf(stderr,"[SL] login HTTP response empty\n");
        goto done;
    }
    ok=parse_login(response.data,response.length,reply);
    if (ok && config->group_chat[0] &&
        !sparkles_sl_uuid_parse(config->group_chat,reply->group_chat))
        ok=0;
    if (ok) {
        int n=snprintf(reply->avatar_name,sizeof reply->avatar_name,
                       "%s %s",config->first,config->last);
        if (n<1 || (size_t)n>=sizeof reply->avatar_name) {
            memset(reply,0,sizeof *reply);
            ok=0;
        }
    }
    if (!ok)
        fprintf(stderr,"[SL] login XML-RPC result rejected or unrecognized (response withheld)\n");
done:
    OPENSSL_cleanse(secret,sizeof secret);
    OPENSSL_cleanse(hashed,sizeof hashed);
    if (request) { OPENSSL_cleanse(request->content,request->use); xmlBufferFree(request); }
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    free(response.data);
    return ok;
}

int sparkles_sl_login_from_file(sparkles_sl_login_reply_t *reply)
{
    sparkles_sl_login_config_t config = {0};
    int ok;
    if (!sparkles_sl_config_load(SPARKLES_SL_CONF_PATH, &config)) {
        fprintf(stderr,"[SL] login config unreadable or invalid: %s (no secrets logged)\n",
                SPARKLES_SL_CONF_PATH);
        return 0;
    }
    ok = sparkles_sl_login(&config, reply);
    sparkles_sl_config_clear(&config);
    return ok;
}
