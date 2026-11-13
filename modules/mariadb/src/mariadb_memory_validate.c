#include <string.h>
#include "sparkles_memory.h"
#include "mariadb_internal.h"

int sparkles_mariadb_memory_valid(
    const sparkles_memory_exchange_request_t *request)
{
    size_t i,account_len,text_len;
    if(!request ||
       !memchr(request->account,0,sizeof request->account) ||
       !memchr(request->utterance,0,sizeof request->utterance))return 0;
    account_len=strlen(request->account);
    text_len=strlen(request->utterance);
    if(request->record>1U||request->ambient>2U||
       (request->ambient && (!request->record ||
        strncmp(request->account,"sl:",3U)))||!account_len||account_len>127U||!text_len||
       text_len>SPARKLES_MEMORY_TEXT_MAX)return 0;
    /* Native SL transport identifies the caller as sl:<UUID>. Treat it
     * as a platform-scoped observation namespace, NOT calendar authority.
     * Strict UUID format prevents arbitrary memory namespace injection. */
    if(!strncmp(request->account,"sl:",3U)) {
        static const unsigned int hyphen_at[]={8U,13U,18U,23U};
        if(account_len!=39U)return 0;
        for(i=0U;i<36U;++i) {
            unsigned char c=(unsigned char)request->account[i+3U];
            int sep=i==hyphen_at[0]||i==hyphen_at[1]||
                    i==hyphen_at[2]||i==hyphen_at[3];
            if(sep ? c!='-' : !((c>='0'&&c<='9')||
                     (c>='a'&&c<='f')))return 0;
        }
    } else {
        for(i=0U;i<account_len;++i) {
            unsigned char c=(unsigned char)request->account[i];
            if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-'||c=='['||
                 c==']'||c=='\\'||c=='^'||c=='{'||
                 c=='}'||c=='|'||c=='.'))return 0;
        }
    }
    for(i=0U;i<text_len;++i) {
        unsigned char c=(unsigned char)request->utterance[i];
        if(c<0x20U||c==0x7fU)return 0;
    }
    return 1;
}

int sparkles_mariadb_memory_selftests(
    unsigned int *executed,unsigned int *passed)
{
    sparkles_memory_exchange_request_t req={0};
    unsigned int e=0,p=0;
#define CHECK(condition) do{++e;if(condition)++p;}while(0)
    memcpy(req.account,"PoeMei",7U);
    memcpy(req.utterance,"Hi Sparkles, I work with DJ Rachel.",35U);
    CHECK(sparkles_mariadb_memory_valid(&req));
    CHECK(!sparkles_mariadb_memory_valid(NULL));
    req.account[0]='\0';
    CHECK(!sparkles_mariadb_memory_valid(&req));
    memcpy(req.account,"PoeMei",7U);
    req.account[0]='@';
    CHECK(!sparkles_mariadb_memory_valid(&req));
    memcpy(req.account,"PoeMei",7U);
    req.utterance[0]='\0';
    CHECK(!sparkles_mariadb_memory_valid(&req));
    memcpy(req.utterance,"Hi\nthere",9U);
    CHECK(!sparkles_mariadb_memory_valid(&req));
    memset(&req,0,sizeof req);
    memcpy(req.utterance,"My name is Mei.",16U);
    memcpy(req.account,"sl:12345678-1234-1234-1234-123456789abc",40U);
    CHECK(sparkles_mariadb_memory_valid(&req));
    req.account[3U]='z';
    CHECK(!sparkles_mariadb_memory_valid(&req));
    req.account[3U]='1';
    req.account[11U]='a';
    CHECK(!sparkles_mariadb_memory_valid(&req));
    req.account[11U]='-';
    CHECK(sparkles_mariadb_memory_valid(&req));
    req.record=1U;
    req.ambient=1U;
    CHECK(sparkles_mariadb_memory_valid(&req));
    req.record=0U;
    CHECK(!sparkles_mariadb_memory_valid(&req));
    req.record=1U;
    req.ambient=2U;
    CHECK(sparkles_mariadb_memory_valid(&req));
    req.ambient=3U;
    CHECK(!sparkles_mariadb_memory_valid(&req));
    req.ambient=1U;
    memcpy(req.account,"PoeMei",7U);
    CHECK(!sparkles_mariadb_memory_valid(&req));
#undef CHECK
    if(executed)*executed=e;
    if(passed)*passed=p;
    return e==p;
}
