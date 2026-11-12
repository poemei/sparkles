#include <mysql.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "sparkles_contacts.h"

/* No schema migration, and no guessing user-specific fields. Every name
 * placed into SQL comes from the fixed list of recognized column names,
 * confirmed against information_schema for the live database. */
typedef struct {
    char id[48], first[48], display[48];
    char fk[48], provider[48], value[48], identity_display[48];
    char provider_value[32];
    int supported;
} contact_schema_t;
static int copy_col(char target[48],const char *name)
{
    size_t n;
    if (!name || (n=strlen(name))>=48U || !n) return 0;
    memcpy(target,name,n+1U);
    return 1;
}
static int col_in(const char *name,const char *const *choices)
{
    unsigned int i;
    for (i=0U;choices[i];++i)
        if (!strcasecmp(name,choices[i]))return 1;
    return 0;
}
static int uuid_account(const char *account)
{
    size_t i;
    if (!account || strlen(account)!=39U ||
        strncmp(account,"sl:",3U)) return 0;
    for(i=0U;i<36U;++i) {
        char c=account[i+3U];
        if(i==8U||i==13U||i==18U||i==23U) {
            if(c!='-')return 0;
        } else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
            return 0;
    }
    return 1;
}
int sparkles_contact_name_parse(const char *message,char name[64])
{
    static const char *const prefixes[]={
        "my name is ","call me ","i'm ","i am ",NULL
    };
    size_t i, j, n;
    const char *p=NULL;
    if (!message || !name) return 0;
    name[0]='\0';
    while(*message==' ')++message;
    for(i=0U;prefixes[i];++i) {
        n=strlen(prefixes[i]);
        if(!strncasecmp(message,prefixes[i],n)) {
            p=message+n;break;
        }
    }
    if(!p) return 0;
    while(*p==' ')++p;
    /* Preserve only an explicitly volunteered short first name.
     * Reject ambiguous or instruction-bearing free text. */
    for(j=0U;j<63U && p[j] && p[j]!=' ' && p[j]!='.' &&
        p[j]!=',' && p[j]!='!' && p[j]!='?' && p[j]!='\n';++j) {
        unsigned char c=(unsigned char)p[j];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             c=='-'||c=='\''))return 0;
        name[j]=(char)c;
    }
    if(!j || j>48U || (p[j] && p[j]!=' ' && p[j]!='.' &&
       p[j]!=',' && p[j]!='!' && p[j]!='?'))return 0;
    name[j]='\0';
    return 1;
}
static int load_columns(MYSQL *db,contact_schema_t *s)
{
    static const char *const c_id[]={"id","contact_id",NULL};
    static const char *const c_first[]={"first_name","firstname","firstName","First Name",NULL};
    static const char *const c_display[]={"display_name","name",NULL};
    static const char *const i_fk[]={"contact_id",NULL};
    static const char *const i_provider[]={"platform","provider","source","identity_type","identity_platform","identity_source",NULL};
    static const char *const i_value[]={"identity_value","platform_user_id","external_uuid",
        "external_id","platform_id","identifier","agent_uuid","avatar_uuid","uuid",NULL};
    static const char *const i_display[]={"display_name","agent_name","username",NULL};
    MYSQL_RES *res;
    MYSQL_ROW row;
    if(mysql_query(db,
        "SELECT table_name,column_name,column_type "
        "FROM information_schema.columns "
        "WHERE table_schema=DATABASE() AND "
        "table_name IN ('contacts','contact_identities')"))return 0;
    res=mysql_store_result(db);
    if(!res)return 0;
    memset(s,0,sizeof *s);
    memcpy(s->provider_value,"second_life",12U);
    while((row=mysql_fetch_row(res))!=NULL) {
        const char *table=row[0],*name=row[1],*type=row[2];
        if(!table||!name||!type)continue;
        if(!strcmp(table,"contacts")) {
            if(!s->id[0]&&col_in(name,c_id))copy_col(s->id,name);
            if(!s->first[0]&&col_in(name,c_first))copy_col(s->first,name);
            if(!s->display[0]&&col_in(name,c_display))copy_col(s->display,name);
        } else if(!strcmp(table,"contact_identities")) {
            if(!s->fk[0]&&col_in(name,i_fk))copy_col(s->fk,name);
            if(!s->provider[0]&&col_in(name,i_provider)) {
                copy_col(s->provider,name);
                if(!strncasecmp(type,"enum(",5U)) {
                    if(strstr(type,"'second_life'"))
                        memcpy(s->provider_value,"second_life",12U);
                    else if(strstr(type,"'SL'"))
                        memcpy(s->provider_value,"SL",3U);
                    else if(strstr(type,"'SECOND_LIFE'"))
                        memcpy(s->provider_value,"SECOND_LIFE",12U);
                    else if(strstr(type,"'Second Life'"))
                        memcpy(s->provider_value,"Second Life",12U);
                    else s->provider_value[0]='\0';
                }
            }
            if(!s->value[0]&&col_in(name,i_value))copy_col(s->value,name);
            if(!s->identity_display[0]&&col_in(name,i_display))
                copy_col(s->identity_display,name);
        }
    }
    mysql_free_result(res);
    s->supported=s->id[0]&&s->first[0]&&s->fk[0]&&
                 s->provider[0]&&s->value[0]&&s->provider_value[0];
    return s->supported;
}
static void bind_string(MYSQL_BIND *b,char *value,unsigned long *len)
{
    memset(b,0,sizeof *b);
    *len=(unsigned long)strlen(value);
    b->buffer_type=MYSQL_TYPE_STRING;
    b->buffer=value;
    b->buffer_length=*len;
    b->length=len;
}
static void bind_id(MYSQL_BIND *b,unsigned long long *id)
{
    memset(b,0,sizeof *b);
    b->buffer_type=MYSQL_TYPE_LONGLONG;
    b->buffer=id;
    b->is_unsigned=1;
}
static int prepared_execute(MYSQL *db,const char *sql, MYSQL_BIND *params,
                            unsigned long long *inserted_id)
{
    MYSQL_STMT *st=mysql_stmt_init(db);
    int ok=st!=NULL;
    if(ok && mysql_stmt_prepare(st,sql,(unsigned long)strlen(sql)))ok=0;
    if(ok && mysql_stmt_bind_param(st,params))ok=0;
    if(ok && mysql_stmt_execute(st))ok=0;
    if(ok && inserted_id)*inserted_id=mysql_stmt_insert_id(st);
    if(st)mysql_stmt_close(st);
    return ok;
}
static int find_identity(MYSQL *db,const contact_schema_t *s,
                         char *platform,char *uuid,
                         unsigned long long *id)
{
    char sql[320];
    MYSQL_STMT *st=NULL;
    MYSQL_BIND param[2]={{0}},out={{0}};
    unsigned long lens[2];
    int rc,result=-1;
    if(snprintf(sql,sizeof sql,
        "SELECT `%s` FROM contact_identities WHERE `%s`=? "
        "AND `%s`=? LIMIT 2 FOR UPDATE",
        s->fk,s->provider,s->value)>=(int)sizeof sql)return -1;
    st=mysql_stmt_init(db);
    if(!st)return -1;
    bind_string(&param[0],platform,&lens[0]);
    bind_string(&param[1],uuid,&lens[1]);
    bind_id(&out,id);
    if(mysql_stmt_prepare(st,sql,(unsigned long)strlen(sql)) ||
       mysql_stmt_bind_param(st,param) || mysql_stmt_execute(st) ||
       mysql_stmt_bind_result(st,&out) || mysql_stmt_store_result(st))
        goto done;
    rc=mysql_stmt_fetch(st);
    if(rc==MYSQL_NO_DATA)result=0;
    else if(rc==0 && mysql_stmt_num_rows(st)==1U)result=1;
done:
    mysql_stmt_close(st);
    return result;
}
static int get_first(MYSQL *db,const contact_schema_t *s,
                     unsigned long long *id,char name[64])
{
    char sql[160],buffer[64]={0};
    MYSQL_STMT *st=NULL;
    MYSQL_BIND param={{0}},out={{0}};
    unsigned long length=0U;
    int ok=0,rc;
    if(snprintf(sql,sizeof sql,"SELECT `%s` FROM contacts "
       "WHERE `%s`=? LIMIT 1",s->first,s->id)>=(int)sizeof sql)
        return 0;
    st=mysql_stmt_init(db);
    if(!st)return 0;
    bind_id(&param,id);
    memset(&out,0,sizeof out);
    out.buffer_type=MYSQL_TYPE_STRING;
    out.buffer=buffer;
    out.buffer_length=sizeof buffer-1U;
    out.length=&length;
    if(mysql_stmt_prepare(st,sql,(unsigned long)strlen(sql)) ||
       mysql_stmt_bind_param(st,&param) || mysql_stmt_execute(st) ||
       mysql_stmt_bind_result(st,&out) || mysql_stmt_store_result(st))
        goto done;
    rc=mysql_stmt_fetch(st);
    if(rc==0 && length<sizeof buffer) {
        memcpy(name,buffer,length);
        name[length]='\0';
        ok=1;
    }
done:
    mysql_stmt_close(st);
    return ok;
}
static int valid_name(const char *name)
{
    size_t i,n;
    if(!name || (n=strlen(name))>48U)return 0;
    for(i=0U;i<n;++i) {
        unsigned char c=(unsigned char)name[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             c=='-'||c=='\''))return 0;
    }
    return 1;
}
int sparkles_mariadb_contact_identify(MYSQL *db,
    const sparkles_contact_request_t *req,sparkles_contact_reply_t *out)
{
    contact_schema_t s;
    unsigned long long id=0ULL;
    unsigned long lens[3]={0};
    MYSQL_BIND params[3]={{0}};
    char uuid[37],sql[384];
    int found,ok=0;
    if(!db||!req||!out||!uuid_account(req->account) ||
       !memchr(req->avatar_name,0,sizeof req->avatar_name) ||
       !memchr(req->first_name,0,sizeof req->first_name) ||
       !valid_name(req->first_name))
        return 0;
    memset(out,0,sizeof *out);
    if(!load_columns(db,&s)) {
        fprintf(stderr,"[MARIADB] contact identity schema unsupported; no write attempted\n");
        return 0;
    }
    memcpy(uuid,req->account+3U,37U);
    if(mysql_query(db,"START TRANSACTION"))return 0;
    found=find_identity(db,&s,s.provider_value,uuid,&id);
    if(found<0)goto done;
    if(found) {
        out->found=1U;
        if(req->first_name[0]) {
            if(snprintf(sql,sizeof sql,
               "UPDATE contacts SET `%s`=? WHERE `%s`=?",
               s.first,s.id)>=(int)sizeof sql)goto done;
            bind_string(&params[0],(char *)req->first_name,&lens[0]);
            bind_id(&params[1],&id);
            if(!prepared_execute(db,sql,params,NULL))goto done;
            out->saved=1U;
        }
        if(!get_first(db,&s,&id,out->first_name) ||
           !valid_name(out->first_name))goto done;
    } else {
        if(!req->first_name[0]) {
            /* Unknown avatar is not a verified human contact.
             * Avoid silently creating a nameless contact. */
            ok=1;
            goto done;
        }
        if(s.display[0]) {
            if(snprintf(sql,sizeof sql,
                 "INSERT INTO contacts (`%s`,`%s`) VALUES (?,?)",
                 s.first,s.display)>=(int)sizeof sql)goto done;
            bind_string(&params[0],(char *)req->first_name,&lens[0]);
            bind_string(&params[1],(char *)req->avatar_name,&lens[1]);
        } else {
            if(snprintf(sql,sizeof sql,
                 "INSERT INTO contacts (`%s`) VALUES (?)",
                 s.first)>=(int)sizeof sql)goto done;
            bind_string(&params[0],(char *)req->first_name,&lens[0]);
        }
        if(!prepared_execute(db,sql,params,&id) || !id)goto done;
        if(s.identity_display[0]) {
            if(snprintf(sql,sizeof sql,
                "INSERT INTO contact_identities (`%s`,`%s`,`%s`,`%s`) "
                "VALUES (?,?,?,?)",s.fk,s.provider,s.value,
                s.identity_display)>=(int)sizeof sql)goto done;
        } else {
            if(snprintf(sql,sizeof sql,
                "INSERT INTO contact_identities (`%s`,`%s`,`%s`) "
                "VALUES (?,?,?)",s.fk,s.provider,s.value)>=(int)sizeof sql)
                goto done;
        }
        bind_id(&params[0],&id);
        bind_string(&params[1],s.provider_value,&lens[1]);
        bind_string(&params[2],uuid,&lens[2]);
        if(s.identity_display[0]) {
            /* Four parameters need their own binding array. */
            MYSQL_BIND four[4]={{0}};
            unsigned long avatar_len;
            memcpy(four,params,sizeof params);
            bind_string(&four[3],(char *)req->avatar_name,&avatar_len);
            if(!prepared_execute(db,sql,four,NULL))goto done;
        } else if(!prepared_execute(db,sql,params,NULL))goto done;
        out->found=out->saved=1U;
        memcpy(out->first_name,req->first_name,strlen(req->first_name)+1U);
    }
    ok=1;
done:
    if(ok && mysql_query(db,"COMMIT"))ok=0;
    if(!ok) {
        (void)mysql_query(db,"ROLLBACK");
        memset(out,0,sizeof *out);
        fprintf(stderr,"[MARIADB] contact identity transaction failed (details withheld)\n");
    }
    return ok;
}
