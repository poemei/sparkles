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
    if(request->record>1U||!account_len||account_len>127U||!text_len||
       text_len>SPARKLES_MEMORY_TEXT_MAX)return 0;
    for(i=0U;i<account_len;++i) {
        unsigned char c=(unsigned char)request->account[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='['||
             c==']'||c=='\\'||c=='^'||c=='{'||
             c=='}'||c=='|'||c=='.'))return 0;
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
    sparkles_memory_exchange_request_t req={{0}};
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
#undef CHECK
    if(executed)*executed=e;
    if(passed)*passed=p;
    return e==p;
}
