#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mariadb_internal.h"

/* Account names originate exclusively from the TLS IRC server account-tag.
 * The allowlist is checked on every request to permit immediate revocation. */
int sparkles_mariadb_account_allowed(const char *path, const char *account)
{
    FILE *fp;
    struct stat st;
    char line[256];
    int allowed=0;
    size_t i,n;
    if (!path || !account || !*account) return 0;
    n=strlen(account);
    if (n>=128U) return 0;
    for (i=0U;i<n;++i) {
        unsigned char ch=(unsigned char)account[i];
        if (!(isalnum(ch) || ch=='_' || ch=='-' || ch=='.')) return 0;
    }
    if (stat(path,&st) || !S_ISREG(st.st_mode) ||
        (st.st_mode & 077U) || st.st_uid != geteuid()) return 0;
    fp=fopen(path,"r");
    if (!fp) return 0;
    while (fgets(line,sizeof line,fp)) {
        size_t len=strlen(line);
        if (!len || line[len-1U]!='\n') { allowed=0; break; }
        line[--len]='\0';
        if (!len || len>=128U || strchr(line,'\r')) {allowed=0;break;}
        for (i=0U;i<len;++i) {
            unsigned char ch=(unsigned char)line[i];
            if (!(isalnum(ch) || ch=='_' || ch=='-' || ch=='.')) break;
        }
        if (i!=len) { allowed=0; break; }
        if (!strcasecmp(line,account)) allowed=1;
    }
    if (ferror(fp)) allowed=0;
    fclose(fp);
    return allowed;
}


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
