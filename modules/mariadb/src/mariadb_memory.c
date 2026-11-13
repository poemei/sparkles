#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include "sparkles_memory.h"
#include "mariadb_internal.h"

/* Independent write: a failed history lookup must never suppress the
 * speaker's new observation. Every insert uses bound values. */
static int context_write(MYSQL *db,const sparkles_memory_exchange_request_t *in)
{
    static const char write_sql[] =
        "INSERT INTO conversation_context "
        "(account,subject,category,content,context_type,source,"
        "verification,visibility,status) "
        "VALUES (?,?,?,?,'OBSERVATION',?,"
        "'UNVERIFIED',?,'ACTIVE')";
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND bind[6]={{0}};
    unsigned long lengths[6];
    const char *category=in->ambient==2U?"group_chat":
                         in->ambient==1U?"local_chat":"conversation";
    const char *source=!strncmp(in->account,"sl:",3U)?"SL":"IRC";
    const char *visibility=in->ambient?"SHARED":"PRIVATE";
    int okay=1;
    lengths[0]=lengths[1]=(unsigned long)strlen(in->account);
    lengths[2]=(unsigned long)strlen(category);
    lengths[3]=(unsigned long)strlen(in->utterance);
    lengths[4]=(unsigned long)strlen(source);
    lengths[5]=(unsigned long)strlen(visibility);
    stmt=mysql_stmt_init(db);
    if(!stmt)return 0;
    if(mysql_stmt_prepare(stmt,write_sql,(unsigned long)strlen(write_sql)))okay=0;
    bind[0].buffer_type=MYSQL_TYPE_STRING;
    bind[0].buffer=(void *)in->account;
    bind[0].buffer_length=lengths[0];bind[0].length=&lengths[0];
    bind[1].buffer_type=MYSQL_TYPE_STRING;
    bind[1].buffer=(void *)in->account;
    bind[1].buffer_length=lengths[1];bind[1].length=&lengths[1];
    bind[2].buffer_type=MYSQL_TYPE_STRING;
    bind[2].buffer=(void *)category;
    bind[2].buffer_length=lengths[2];bind[2].length=&lengths[2];
    bind[3].buffer_type=MYSQL_TYPE_STRING;
    bind[3].buffer=(void *)in->utterance;
    bind[3].buffer_length=lengths[3];bind[3].length=&lengths[3];
    bind[4].buffer_type=MYSQL_TYPE_STRING;
    bind[4].buffer=(void *)source;
    bind[4].buffer_length=lengths[4];bind[4].length=&lengths[4];
    bind[5].buffer_type=MYSQL_TYPE_STRING;
    bind[5].buffer=(void *)visibility;
    bind[5].buffer_length=lengths[5];bind[5].length=&lengths[5];
    if(okay && mysql_stmt_bind_param(stmt,bind))okay=0;
    if(okay && mysql_stmt_execute(stmt))okay=0;
    if(okay && mysql_stmt_affected_rows(stmt)!=1ULL)okay=0;
    if(!okay)
        fprintf(stderr,"[MARIADB] context write failed: stmt_errno=%u (content withheld)\n",
                mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    return okay;
}

/* All data is scoped to a server-verified account. Each observation is an
 * unverified statement by the speaker, not a verified third-party fact.
 * This module does not resolve or assert cross-transport identity.
 * SL observations are segregated by verified simulator UUID namespace. */
int sparkles_mariadb_context_exchange(
    MYSQL *db,
    const sparkles_memory_exchange_request_t *in,
    sparkles_memory_exchange_reply_t *out)
{
    static const char read_sql[] =
        "SELECT LEFT(content,120) FROM conversation_context "
        "WHERE account=? AND status='ACTIVE' "
        "AND (expires_at IS NULL OR expires_at>NOW()) "
        "AND visibility IN ('PRIVATE','SHARED') "
        "ORDER BY updated_at DESC,id DESC LIMIT 3";
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND bind={0}, col={0};
    unsigned long account_length=0U,found_len=0U;
    char content[512];
    char *account;
    int rc,okay=1;
    unsigned int i;
    size_t used=0U;
    if(!db||!in||!out||!sparkles_mariadb_memory_valid(in))
        return 0;
    memset(out,0,sizeof *out);
    account=(char *)in->account;
    account_length=(unsigned long)strlen(in->account);
    stmt=mysql_stmt_init(db);
    if(!stmt) {
        if(!in->record)return 0;
        if(!context_write(db,in))return 0;
        out->saved=1U;
        return 1;
    }
    if(mysql_stmt_prepare(stmt,read_sql,(unsigned long)strlen(read_sql)))okay=0;
    bind.buffer_type=MYSQL_TYPE_STRING;
    bind.buffer=account;
    bind.buffer_length=account_length;
    bind.length=&account_length;
    if(okay&&mysql_stmt_bind_param(stmt,&bind))okay=0;
    if(okay&&mysql_stmt_execute(stmt))okay=0;
    memset(content,0,sizeof content);
    col.buffer_type=MYSQL_TYPE_STRING;
    col.buffer=content;
    col.buffer_length=sizeof content-1U;
    col.length=&found_len;
    if(okay&&mysql_stmt_bind_result(stmt,&col))okay=0;
    if(okay&&mysql_stmt_store_result(stmt))okay=0;
    for(i=0U;okay&&i<3U;++i) {
        int written;
        rc=mysql_stmt_fetch(stmt);
        if(rc==MYSQL_NO_DATA)break;
        if(rc!=0||found_len>=sizeof content) {okay=0;break;}
        content[found_len]='\0';
        /* Explicitly label memories as previous statements, not facts. */
        written=snprintf(out->previous+used,
                         sizeof out->previous-used,
                         "%sEarlier message from this account: %s",
                         used?"\n":"",content);
        if(written<0||(size_t)written>=sizeof out->previous-used)break;
        used+=(size_t)written;
        ++out->recalled;
    }
    if(!okay)
        fprintf(stderr,"[MARIADB] context read failed: stmt_errno=%u (content withheld)\n",
                mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    if(!okay) {
        out->recalled=0U;
        out->previous[0]=0;
    }
    /* A retrieval error must not suppress the new observation. Record only
     * after reading to avoid presenting the current utterance as history. */
    if(in->record) {
        if(!context_write(db,in))return 0;
        out->saved=1U;
        return 1;
    }
    return okay;
}
