#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include "mariadb_internal.h"

/* Calendar access is a property of an existing contact with a verified
 * transport identity, not of an IRC-specific server allowlist.
 * The Core transport supplies the authenticated principal. MariaDB
 * resolves it to a contact on every request for immediate consistency.
 * Call under the MariaDB module's db_lock. */
int sparkles_mariadb_identity_parts(const char *account,
                                   const char **platform,
                                   const char **identifier)
{
    size_t i,n;
    if(!account||!platform||!identifier)return 0;
    { const char *end=memchr(account,0,128U);
      if(!end)return 0;
      n=(size_t)(end-account); }
    if(!n||n>=128U)return 0;
    if(!strncmp(account,"sl:",3U)) {
        const char *uuid=account+3U;
        if(n!=39U)return 0;
        for(i=0U;i<36U;++i) {
            unsigned char c=(unsigned char)uuid[i];
            if(i==8U||i==13U||i==18U||i==23U) {
                if(c!='-')return 0;
            } else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
                return 0;
        }
        *platform="second_life";
        *identifier=uuid;
        return 1;
    }
    /* Unauthenticated or anonymous IRC tags must never gain access.
     * Do not use sender nickname or a user-supplied display name. */
    if(!strcmp(account,"*")||!strcmp(account,"0"))return 0;
    for(i=0U;i<n;++i) {
        unsigned char c=(unsigned char)account[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||
             c=='.'||c=='['||c==']'||c=='\\'||
             c=='^'||c=='{'||c=='}'||c=='|'))return 0;
    }
    *platform="irc";
    *identifier=account;
    return 1;
}
int sparkles_mariadb_contact_calendar_allowed(MYSQL *db,const char *account)
{
    static const char sql[]=
        "SELECT c.id FROM contact_identities i "
        "INNER JOIN contacts c ON c.id=i.contact_id "
        "WHERE i.platform=? AND i.platform_identifier=? LIMIT 2";
    const char *platform=NULL,*identity=NULL;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND binds[2]={{0}},value={{0}};
    unsigned long lengths[2];
    unsigned long long contact_id=0ULL;
    int allowed=0,rc;
    if(!db||!sparkles_mariadb_identity_parts(account,&platform,&identity))
        return 0;
    stmt=mysql_stmt_init(db);
    if(!stmt)return 0;
    lengths[0]=(unsigned long)strlen(platform);
    lengths[1]=(unsigned long)strlen(identity);
    binds[0].buffer_type=MYSQL_TYPE_STRING;
    binds[0].buffer=(void *)platform;
    binds[0].buffer_length=lengths[0];
    binds[0].length=&lengths[0];
    binds[1].buffer_type=MYSQL_TYPE_STRING;
    binds[1].buffer=(void *)identity;
    binds[1].buffer_length=lengths[1];
    binds[1].length=&lengths[1];
    value.buffer_type=MYSQL_TYPE_LONGLONG;
    value.buffer=&contact_id;
    value.is_unsigned=1;
    if(mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql)) ||
       mysql_stmt_bind_param(stmt,binds) || mysql_stmt_execute(stmt) ||
       mysql_stmt_bind_result(stmt,&value) || mysql_stmt_store_result(stmt))
        goto done;
    rc=mysql_stmt_fetch(stmt);
    if(rc==0 && contact_id>0ULL && mysql_stmt_num_rows(stmt)==1U)
        allowed=1;
done:
    mysql_stmt_close(stmt);
    return allowed;
}

/* These helpers require the caller to hold db_lock. They never infer
 * relationship ownership from contact names or free-form appointment text. */
int sparkles_mariadb_contact_id(MYSQL *db,const char *account,
                               unsigned long long *contact_id)
{
    static const char sql[]=
        "SELECT c.id FROM contact_identities i "
        "JOIN contacts c ON c.id=i.contact_id "
        "WHERE i.platform=? AND i.platform_identifier=? LIMIT 2";
    const char *platform=NULL,*identity=NULL;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND in[2]={{0}},out={{0}};
    unsigned long lengths[2];
    unsigned long long id=0ULL;
    int found=0;
    if(contact_id)*contact_id=0ULL;
    if(!db||!contact_id||
       !sparkles_mariadb_identity_parts(account,&platform,&identity))
        return 0;
    stmt=mysql_stmt_init(db);
    if(!stmt)return 0;
    lengths[0]=(unsigned long)strlen(platform);
    lengths[1]=(unsigned long)strlen(identity);
    in[0].buffer_type=MYSQL_TYPE_STRING;
    in[0].buffer=(void *)platform;
    in[0].buffer_length=lengths[0];in[0].length=&lengths[0];
    in[1].buffer_type=MYSQL_TYPE_STRING;
    in[1].buffer=(void *)identity;
    in[1].buffer_length=lengths[1];in[1].length=&lengths[1];
    out.buffer_type=MYSQL_TYPE_LONGLONG;
    out.buffer=&id;out.is_unsigned=1;
    if(mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql))||
       mysql_stmt_bind_param(stmt,in)||mysql_stmt_execute(stmt)||
       mysql_stmt_bind_result(stmt,&out)||mysql_stmt_store_result(stmt))
        goto done;
    if(mysql_stmt_num_rows(stmt)==1ULL && mysql_stmt_fetch(stmt)==0 &&
       id!=0ULL){*contact_id=id;found=1;}
done:
    mysql_stmt_close(stmt);
    return found;
}
/* Existing schema only: require these two linkage columns; refuse layouts
 * with unknown mandatory insert columns rather than inventing values. */
int sparkles_mariadb_participants_supported(MYSQL *db)
{
    static const char sql[]=
      "SELECT column_name,is_nullable,column_default,extra "
      "FROM information_schema.columns "
      "WHERE table_schema=DATABASE() AND table_name='appointment_participants'";
    MYSQL_RES *res=NULL;
    MYSQL_ROW row;
    int appointment=0,contact=0,okay=1;
    if(!db||mysql_query(db,sql))return 0;
    res=mysql_store_result(db);
    if(!res)return 0;
    while((row=mysql_fetch_row(res))!=NULL) {
        const char *name=row[0],*nullable=row[1],*def=row[2],*extra=row[3];
        if(!name||!nullable) {okay=0;break;}
        if(!strcmp(name,"appointment_id"))appointment=1;
        else if(!strcmp(name,"contact_id"))contact=1;
        else if(!strcmp(nullable,"NO")&&!def &&
                (!extra||!strstr(extra,"auto_increment"))) {
            fprintf(stderr,
               "[MARIADB] appointment_participants has unsupported required column (values withheld)\n");
            okay=0;
        }
    }
    mysql_free_result(res);
    return okay&&appointment&&contact;
}
int sparkles_mariadb_participant_add(MYSQL *db,
                                    unsigned long long appointment_id,
                                    unsigned long long contact_id)
{
    /* The creating contact explicitly confirmed this appointment.
     * Their own participant association is accepted, not pending. */
    static const char sql[]=
        "INSERT INTO appointment_participants "
        "(appointment_id,contact_id,response_status) "
        "VALUES (?,?,'accepted')";
    MYSQL_STMT *stmt;
    MYSQL_BIND in[2]={{0}};
    int okay=0;
    if(!db||!appointment_id||!contact_id)return 0;
    stmt=mysql_stmt_init(db);
    if(!stmt)return 0;
    in[0].buffer_type=MYSQL_TYPE_LONGLONG;
    in[0].buffer=&appointment_id;in[0].is_unsigned=1;
    in[1].buffer_type=MYSQL_TYPE_LONGLONG;
    in[1].buffer=&contact_id;in[1].is_unsigned=1;
    if(!mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql)) &&
       !mysql_stmt_bind_param(stmt,in) && !mysql_stmt_execute(stmt) &&
       mysql_stmt_affected_rows(stmt)==1ULL)okay=1;
    if(!okay)fprintf(stderr,
       "[MARIADB] appointment participant insert failed: sql_errno=%u (values withheld)\n",
       mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    return okay;
}

/* Guest association remains pending until that contact responds. */
int sparkles_mariadb_participant_invite(MYSQL *db,
                                       unsigned long long appointment_id,
                                       unsigned long long contact_id)
{
    static const char sql[]=
        "INSERT INTO appointment_participants "
        "(appointment_id,contact_id,response_status) "
        "VALUES (?,?,'pending')";
    MYSQL_STMT *stmt;
    MYSQL_BIND in[2]={{0}};
    int okay=0;
    if(!db||!appointment_id||!contact_id)return 0;
    stmt=mysql_stmt_init(db);
    if(!stmt)return 0;
    in[0].buffer_type=MYSQL_TYPE_LONGLONG;
    in[0].buffer=&appointment_id;in[0].is_unsigned=1;
    in[1].buffer_type=MYSQL_TYPE_LONGLONG;
    in[1].buffer=&contact_id;in[1].is_unsigned=1;
    if(!mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql))&&
       !mysql_stmt_bind_param(stmt,in)&&!mysql_stmt_execute(stmt)&&
       mysql_stmt_affected_rows(stmt)==1ULL)okay=1;
    if(!okay)fprintf(stderr,
       "[MARIADB] guest participant insert failed: sql_errno=%u (values withheld)\n",
       mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    return okay;
}
