#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include "mariadb_internal.h"
#include "sparkles_show.h"

/* Called with the existing MariaDB module connection lock held.
 * Only explicitly registered public shows qualify; a calendar appointment
 * alone is NEVER a public announcement. All timestamps use DB-local NOW().
 * Claims are at-most-once: uncertain delivery is investigated, not retried. */
static const char reminder_sql[] =
    "SELECT e.id, LOWER(e.owner_avatar_uuid), a.title, "
    "DATE_FORMAT(a.start_time,'%Y-%m-%d %H:%i'),e.venue,e.dj_name,0 "
    "FROM show_events e JOIN appointments a ON a.id=e.appointment_id "
    "WHERE e.status='scheduled' AND a.status='scheduled' "
    "AND a.start_time>DATE_ADD(NOW(), INTERVAL 50 MINUTE) "
    "AND a.start_time<=DATE_ADD(NOW(), INTERVAL 60 MINUTE) "
    "AND EXISTS (SELECT 1 FROM contact_identities ci "
    "JOIN appointment_participants ap ON ap.contact_id=ci.contact_id "
    "WHERE ci.platform='second_life' "
    "AND LOWER(ci.platform_identifier)=LOWER(e.owner_avatar_uuid) "
    "AND ap.appointment_id=a.id) "
    "AND NOT EXISTS (SELECT 1 FROM show_delivery_attempts d "
    "WHERE d.show_id=e.id AND d.kind='confirmation' AND d.group_id=0) "
    "ORDER BY a.start_time,e.id LIMIT 1";

static const char notice_sql[] =
    "SELECT e.id, LOWER(g.group_uuid), a.title, "
    "DATE_FORMAT(a.start_time,'%Y-%m-%d %H:%i'),e.venue,e.dj_name,g.id "
    "FROM show_events e JOIN appointments a ON a.id=e.appointment_id "
    "JOIN businesses b ON LOWER(b.sl_owner_uuid)=LOWER(e.owner_avatar_uuid) "
    "JOIN business_groups bg ON bg.business_id=b.id "
    "JOIN sl_groups g ON g.id=bg.group_id "
    "WHERE e.status='scheduled' AND a.status='scheduled' "
    "AND b.is_active=1 AND bg.notices_enabled=1 "
    "AND g.is_active=1 AND g.notices_enabled=1 AND g.entertainment_enabled=1 "
    "AND a.start_time>DATE_ADD(NOW(), INTERVAL 20 MINUTE) "
    "AND a.start_time<=DATE_ADD(NOW(), INTERVAL 30 MINUTE) "
    "AND EXISTS (SELECT 1 FROM contact_identities ci "
    "JOIN appointment_participants ap ON ap.contact_id=ci.contact_id "
    "WHERE ci.platform='second_life' "
    "AND LOWER(ci.platform_identifier)=LOWER(e.owner_avatar_uuid) "
    "AND ap.appointment_id=a.id) "
    "AND NOT EXISTS (SELECT 1 FROM show_delivery_attempts d "
    "WHERE d.show_id=e.id AND d.kind='notice' AND d.group_id=g.id) "
    "ORDER BY a.start_time,e.id,g.id LIMIT 1";

static int decimal_id(const char *s,unsigned long long *out)
{
    unsigned long long value=0ULL;
    const unsigned char *p=(const unsigned char *)s;
    if(!p||!*p||!out)return 0;
    for(;*p;++p) {
        if(*p<'0'||*p>'9'||value>(~0ULL-(unsigned)(*p-'0'))/10ULL)
            return 0;
        value=value*10ULL+(unsigned)(*p-'0');
    }
    *out=value;
    return 1;
}
static int valid_uuid(const char *id)
{
    const char *platform=NULL,*identifier=NULL;
    char account[40];
    int n;
    if(!id)return 0;
    n=snprintf(account,sizeof account,"sl:%s",id);
    return n==39 && sparkles_mariadb_identity_parts(account,&platform,&identifier)
        && !strcmp(platform,"second_life");
}
static void sanitize_text(char *dst,size_t cap,const char *src)
{
    size_t i=0U;
    if(!cap)return;
    if(src)for(;src[i] && i+1U<cap;++i) {
        unsigned char c=(unsigned char)src[i];
        dst[i]=(c=='|'?'/':(c=='\n'||c=='\r'||c=='\t'?' ':(char)c));
    }
    dst[i]=0;
}
int sparkles_mariadb_show_claim(MYSQL *connection,
                               sparkles_show_claim_reply_t *out)
{
    MYSQL_RES *res=NULL;
    MYSQL_ROW row;
    unsigned long long event_id=0ULL,group_id=0ULL;
    const char *kind=NULL;
    char sql[256],title[160],venue[152],dj[104],when[20];
    int type,n;
    if(!connection||!out)return 0;
    memset(out,0,sizeof *out);
    if(mysql_query(connection,"START TRANSACTION"))return 0;
    for(type=SPARKLES_SHOW_NOTICE;type>=SPARKLES_SHOW_CONFIRMATION;--type) {
        if(mysql_query(connection,type==SPARKLES_SHOW_NOTICE?
                       notice_sql:reminder_sql))goto rollback;
        res=mysql_store_result(connection);
        if(!res)goto rollback;
        row=mysql_fetch_row(res);
        if(!row) {
            mysql_free_result(res);res=NULL;
            continue;
        }
        if(!row[0]||!row[1]||!row[2]||!row[3]||
           !row[4]||!row[5]||!row[6]||
           !decimal_id(row[0],&event_id) || event_id==0ULL ||
           !decimal_id(row[6],&group_id) ||
           !valid_uuid(row[1]) || strlen(row[3])!=16U)
            goto rollback;
        memcpy(out->action.recipient_uuid,row[1],37U);
        memcpy(when,row[3],17U);
        sanitize_text(title,sizeof title,row[2]);
        sanitize_text(venue,sizeof venue,row[4]);
        sanitize_text(dj,sizeof dj,row[5]);
        out->action.kind=(unsigned int)type;
        if(type==SPARKLES_SHOW_NOTICE) {
            memcpy(out->action.subject,"Show starts in 30 minutes",26U);
            n=snprintf(out->action.message,sizeof out->action.message,
               "Join us at %s on %s for %s%s%s. Starting in 30 minutes!",
               venue,when,title,dj[0]?" - DJ: ":"",dj);
        } else {
            n=snprintf(out->action.message,sizeof out->action.message,
               "Your show at %s is scheduled for %s (%s). "
               "Are you still good to host? Reply 'show yes' or "
               "'show cancel' in this private IM.",
               venue,when,title);
        }
        mysql_free_result(res);res=NULL;
        if(n<1||(size_t)n>=sizeof out->action.message)goto rollback;
        kind=type==SPARKLES_SHOW_NOTICE?"notice":"confirmation";
        n=snprintf(sql,sizeof sql,
           "INSERT IGNORE INTO show_delivery_attempts "
           "(show_id,kind,group_id) VALUES (%llu,'%s',%llu)",
           event_id,kind,group_id);
        if(n<0||(size_t)n>=sizeof sql||
           mysql_query(connection,sql)||
           mysql_affected_rows(connection)!=1ULL)
            goto rollback;
        out->action.attempt_id=mysql_insert_id(connection);
        if(!out->action.attempt_id)goto rollback;
        out->available=1U;
        break;
    }
    if(mysql_query(connection,"COMMIT")) {
        memset(out,0,sizeof *out);
        return 0; /* Unknown commit result: never resend automatically. */
    }
    return 1;
rollback:
    if(res)mysql_free_result(res);
    (void)mysql_query(connection,"ROLLBACK");
    memset(out,0,sizeof *out);
    return 0;
}
int sparkles_mariadb_show_respond(MYSQL *connection,
     const sparkles_show_response_request_t *in,
     sparkles_show_response_reply_t *out)
{
    const char *platform=NULL,*identity=NULL;
    MYSQL_RES *res=NULL;
    MYSQL_ROW row;
    char query[550];
    unsigned long long id=0ULL;
    int n;
    if(!connection||!in||!out)return 0;
    memset(out,0,sizeof *out);
    if(!sparkles_mariadb_identity_parts(in->account,&platform,&identity) ||
       strcmp(platform,"second_life") ||
       (in->answer!=1U && in->answer!=2U))
        return 0;
    if(mysql_query(connection,"START TRANSACTION"))return 0;
    n=snprintf(query,sizeof query,
      "SELECT e.id FROM show_events e "
      "JOIN appointments a ON a.id=e.appointment_id "
      "WHERE LOWER(e.owner_avatar_uuid)='%s' "
      "AND e.status='scheduled' AND a.status='scheduled' "
      "AND a.start_time>NOW() "
      "AND a.start_time<=DATE_ADD(NOW(), INTERVAL 90 MINUTE) "
      "AND EXISTS (SELECT 1 FROM show_delivery_attempts d "
      "WHERE d.show_id=e.id AND d.kind='confirmation') "
      "ORDER BY a.start_time,e.id LIMIT 2",identity);
    if(n<0||(size_t)n>=sizeof query||
       mysql_query(connection,query))goto rollback;
    res=mysql_store_result(connection);
    if(!res)goto rollback;
    if(mysql_num_rows(res)!=1ULL) {
        mysql_free_result(res);res=NULL;
        (void)mysql_query(connection,"ROLLBACK");
        return 1; /* none or ambiguous: never mutate */
    }
    row=mysql_fetch_row(res);
    if(!row||!decimal_id(row[0],&id)||!id)goto rollback;
    mysql_free_result(res);res=NULL;
    n=snprintf(query,sizeof query,
       "UPDATE show_events SET %s WHERE id=%llu AND status='scheduled'",
       in->answer==1U?"confirmed=1":"status='cancelled'",id);
    if(n<0||(size_t)n>=sizeof query||
       mysql_query(connection,query)||mysql_affected_rows(connection)!=1ULL)
        goto rollback;
    if(mysql_query(connection,"COMMIT"))return 0;
    out->updated=1U;
    return 1;
rollback:
    if(res)mysql_free_result(res);
    (void)mysql_query(connection,"ROLLBACK");
    return 0;
}
int sparkles_mariadb_show_attempt(MYSQL *connection,
                                 const sparkles_show_attempt_request_t *in)
{
    char query[230];
    int n;
    if(!connection||!in||!in->attempt_id)return 0;
    n=snprintf(query,sizeof query,
      "UPDATE show_delivery_attempts SET state='%s' "
      "WHERE id=%llu AND state='claimed'",
      in->queued?"queued":"failed",in->attempt_id);
    return n>0&&(size_t)n<sizeof query &&
           mysql_query(connection,query)==0 &&
           mysql_affected_rows(connection)==1ULL;
}
