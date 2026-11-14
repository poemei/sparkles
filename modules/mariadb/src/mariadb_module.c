#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "stnlabz_abi/module.h"
#include "sparkles_mariadb.h"
#include "sparkles_memory.h"
#include "sparkles_contacts.h"
#include "sparkles_sl_group_db.h"
#include "sparkles_show.h"
#include "mariadb_internal.h"

static MYSQL *db;
static pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
static const stnlabz_module_host_t *host_owner;
static int registered;
static int command_registered;
static int add_command_registered;
static int create_service_registered;
static int check_service_registered;
static int memory_service_registered;
static int contacts_service_registered;
static int mutate_service_registered;
static int group_resolve_registered;
static int marketing_groups_registered;
static int show_claim_registered,show_response_registered,show_attempt_registered;

/* Resolve authenticated transport identity to an existing contact. All
 * calendar operations share this gate; lookup itself is serialized with
 * the MariaDB connection lock, independently of later DB operations. */
static int calendar_contact_allowed(const char *account)
{
    int allowed=0;
    (void)pthread_mutex_lock(&db_lock);
    if(db && !mysql_ping(db))
        allowed=sparkles_mariadb_contact_calendar_allowed(db,account);
    (void)pthread_mutex_unlock(&db_lock);
    return allowed;
}

/* Resolve an SL group from the operator-maintained business registry.
 * Neither the LLM nor the SL transport can choose an unregistered UUID.
 * The authenticated private IM sender must be the recorded business owner.
 * A missing owner UUID fails closed until the operator populates it.
 * The business/group link is authoritative for operation ownership. */
static stnlabz_module_result_t group_resolve_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *ctx)
{
    static const char chat_sql[]=
        "SELECT DISTINCT LOWER(g.group_uuid) FROM sl_groups g "
        "JOIN business_groups bg ON bg.group_id=g.id "
        "JOIN businesses b ON b.id=bg.business_id "
        "WHERE g.group_name=? AND g.is_active=1 AND g.chat_enabled=1 "
        "AND b.is_active=1 AND LOWER(b.sl_owner_uuid)=? LIMIT 2";
    static const char notice_sql[]=
        "SELECT DISTINCT LOWER(g.group_uuid) FROM sl_groups g "
        "JOIN business_groups bg ON bg.group_id=g.id "
        "JOIN businesses b ON b.id=bg.business_id "
        "WHERE g.group_name=? AND g.is_active=1 AND g.notices_enabled=1 "
        "AND bg.notices_enabled=1 AND b.is_active=1 "
        "AND LOWER(b.sl_owner_uuid)=? LIMIT 2";
    const char *sql;
    const sparkles_sl_group_resolve_request_t *in=request;
    sparkles_sl_group_resolve_reply_t *out=response;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND args[2]={{0}},field={0};
    const char *platform=NULL,*identity=NULL;
    unsigned long arg_len[2],uuid_len=0U;
    char uuid[37]={0},account[40]={0};
    int okay=1,rc;
    size_t name_len;
    (void)ctx;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||
       response_size<sizeof *out ||
       !memchr(in->account,0,sizeof in->account) ||
       !memchr(in->group_name,0,sizeof in->group_name) ||
       !in->group_name[0] || in->notice>1U)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    sql=in->notice?notice_sql:chat_sql;
    if(!sparkles_mariadb_identity_parts(in->account,&platform,&identity) ||
       strcmp(platform,"second_life"))
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    name_len=strlen(in->group_name);
    if(name_len>63U)return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    arg_len[0]=(unsigned long)name_len;
    arg_len[1]=36UL;
    args[0].buffer_type=MYSQL_TYPE_STRING;
    args[0].buffer=(void *)in->group_name;
    args[0].buffer_length=arg_len[0];args[0].length=&arg_len[0];
    args[1].buffer_type=MYSQL_TYPE_STRING;
    args[1].buffer=(void *)identity;
    args[1].buffer_length=arg_len[1];args[1].length=&arg_len[1];
    field.buffer_type=MYSQL_TYPE_STRING;
    field.buffer=uuid;field.buffer_length=sizeof uuid;
    field.length=&uuid_len;
    (void)pthread_mutex_lock(&db_lock);
    if(!db||mysql_ping(db))okay=0;
    if(okay)stmt=mysql_stmt_init(db);
    if(!stmt)okay=0;
    if(okay && mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql)))okay=0;
    if(okay && mysql_stmt_bind_param(stmt,args))okay=0;
    if(okay && mysql_stmt_execute(stmt))okay=0;
    if(okay && mysql_stmt_bind_result(stmt,&field))okay=0;
    if(okay && mysql_stmt_store_result(stmt))okay=0;
    if(okay && mysql_stmt_num_rows(stmt)==1ULL) {
        rc=mysql_stmt_fetch(stmt);
        if(rc!=0 || uuid_len!=36UL)okay=0;
        else {
            uuid[36]=0;
            if(snprintf(account,sizeof account,"sl:%s",uuid)!=(int)39 ||
               !sparkles_mariadb_identity_parts(account,&platform,&identity))
                okay=0;
            else {
                memcpy(out->group_uuid,uuid,sizeof uuid);
                out->matched=1U;
            }
        }
    }
    if(stmt)mysql_stmt_close(stmt);
    (void)pthread_mutex_unlock(&db_lock);
    if(!okay) {
        memset(out,0,sizeof *out);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    if(!out->matched)return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}


/* Resolve a bounded group-name list for authenticated owner marketing.
 * Every group is subsequently rechecked by the SL module before queueing. */
static stnlabz_module_result_t marketing_groups_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *ctx)
{
    static const char sql[]=
        "SELECT DISTINCT g.group_name FROM sl_groups g "
        "JOIN business_groups bg ON bg.group_id=g.id "
        "JOIN businesses b ON b.id=bg.business_id "
        "WHERE g.is_active=1 AND g.notices_enabled=1 "
        "AND bg.notices_enabled=1 AND b.is_active=1 "
        "AND LOWER(b.sl_owner_uuid)=? "
        "ORDER BY g.group_name LIMIT 17";
    const sparkles_sl_marketing_groups_request_t *in=request;
    sparkles_sl_marketing_groups_reply_t *out=response;
    const char *platform=NULL,*identity=NULL;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND param={0},field={0};
    unsigned long identity_length=36UL,name_length=0U;
    char name[64]={0};
    int okay=1,fetch_rc=MYSQL_NO_DATA;
    (void)ctx;
    if(used)*used=0U;
    if(!in||!out||request_size!=sizeof *in||response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if(!sparkles_mariadb_identity_parts(in->account,&platform,&identity)||
       strcmp(platform,"second_life"))
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    param.buffer_type=MYSQL_TYPE_STRING;
    param.buffer=(void *)identity;
    param.buffer_length=identity_length;
    param.length=&identity_length;
    field.buffer_type=MYSQL_TYPE_STRING;
    field.buffer=name;
    field.buffer_length=sizeof name;
    field.length=&name_length;
    (void)pthread_mutex_lock(&db_lock);
    if(!db||mysql_ping(db))okay=0;
    if(okay)stmt=mysql_stmt_init(db);
    if(!stmt)okay=0;
    if(okay&&mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql)))okay=0;
    if(okay&&mysql_stmt_bind_param(stmt,&param))okay=0;
    if(okay&&mysql_stmt_execute(stmt))okay=0;
    if(okay&&mysql_stmt_bind_result(stmt,&field))okay=0;
    if(okay&&mysql_stmt_store_result(stmt))okay=0;
    if(okay) {
        while((fetch_rc=mysql_stmt_fetch(stmt))==0) {
            if(!name_length||name_length>=sizeof name||
               out->count>=SPARKLES_SL_MARKETING_GROUPS_MAX) {
                okay=0;
                break;
            }
            name[name_length]=0;
            memcpy(out->names[out->count],name,name_length+1U);
            ++out->count;
        }
        if(okay&&fetch_rc!=MYSQL_NO_DATA)okay=0;
    }
    if(stmt)mysql_stmt_close(stmt);
    (void)pthread_mutex_unlock(&db_lock);
    if(!okay||!out->count) {
        memset(out,0,sizeof *out);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Only trusted in-process clients may request the next due action.
 * DB-side unique attempts keep retries/restarts from double-posting. */
static stnlabz_module_result_t shows_claim_service(
    const void *request,size_t request_size,void *response,
    size_t response_size,size_t *used,void *ctx)
{
    sparkles_show_claim_reply_t *out=response;
    int ok=0;
    (void)ctx;
    if(used)*used=0U;
    if(request||request_size||!out||response_size<sizeof *out)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if(db&&!mysql_ping(db))ok=sparkles_mariadb_show_claim(db,out);
    (void)pthread_mutex_unlock(&db_lock);
    if(!ok)return STNLABZ_MODULE_ERR_INVALID_STATE;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t shows_response_service(
    const void *request,size_t request_size,void *response,
    size_t response_size,size_t *used,void *ctx)
{
    const sparkles_show_response_request_t *in=request;
    sparkles_show_response_reply_t *out=response;
    int ok=0;
    (void)ctx;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out ||
       !memchr(in->account,0,sizeof in->account) ||
       (in->answer!=1U&&in->answer!=2U))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if(db&&!mysql_ping(db))ok=sparkles_mariadb_show_respond(db,in,out);
    (void)pthread_mutex_unlock(&db_lock);
    if(!ok)return STNLABZ_MODULE_ERR_INVALID_STATE;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}
static stnlabz_module_result_t shows_attempt_service(
    const void *request,size_t request_size,void *response,
    size_t response_size,size_t *used,void *ctx)
{
    const sparkles_show_attempt_request_t *in=request;
    int ok=0;
    (void)ctx;(void)response;(void)response_size;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!in->attempt_id)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    (void)pthread_mutex_lock(&db_lock);
    if(db&&!mysql_ping(db))ok=sparkles_mariadb_show_attempt(db,in);
    (void)pthread_mutex_unlock(&db_lock);
    return ok?STNLABZ_MODULE_OK:STNLABZ_MODULE_ERR_INVALID_STATE;
}

static unsigned int schema_table_count(MYSQL *connection)
{
    static const char sql[] =
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name IN ("
        "'contacts','appointments','tasks','appointment_participants',"
        "'interactions','contact_identities','contact_permissions')";
    MYSQL_RES *res;
    MYSQL_ROW row;
    unsigned long n;
    char *end;
    if (mysql_query(connection, sql)) return 0U;
    res = mysql_store_result(connection);
    if (!res) return 0U;
    row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        mysql_free_result(res);
        return 0U;
    }
    n = strtoul(row[0], &end, 10);
    if (!*row[0] || *end || n > SPARKLES_MARIADB_EXPECTED_TABLES)
        n = 0;
    mysql_free_result(res);
    return (unsigned int)n;
}

static stnlabz_module_result_t status_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *used, void *ctx)
{
    sparkles_mariadb_status_t *out = response;
    unsigned int count;
    (void)ctx;
    if (used) *used = 0U;
    if (request || request_size || !out || response_size < sizeof *out)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if (!db || mysql_ping(db)) {
        (void)pthread_mutex_unlock(&db_lock);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    count = schema_table_count(db);
    (void)pthread_mutex_unlock(&db_lock);
    if (count != SPARKLES_MARIADB_EXPECTED_TABLES)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    out->connected = 1U;
    out->tables_found = count;
    if (used) *used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Authenticated, read-only, bounded date-range query. Both explicit
 * commands and trusted in-process consumers provide verified identities. */
static stnlabz_module_result_t appointments_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *used, void *ctx)
{
    static const char sql[] =
        "SELECT a.id, a.title, "
        "DATE_FORMAT(a.start_time,'%Y-%m-%d %H:%i:%s'), "
        "DATE_FORMAT(a.end_time,'%Y-%m-%d %H:%i:%s') "
        "FROM appointments a "
        "WHERE a.status='scheduled' AND a.start_time < ? AND a.end_time > ? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=a.id AND ap.contact_id=?) "
        "ORDER BY a.start_time, a.id LIMIT 9";
    const sparkles_mariadb_appointments_request_t *in = request;
    sparkles_mariadb_appointments_reply_t *out = response;
    MYSQL_STMT *stmt = NULL;
    MYSQL_BIND params[3] = {{0}}, result[4] = {{0}};
    char from[20], until[20], title[801], start[20], end[20];
    unsigned long title_len = 0U, start_len = 0U, end_len = 0U;
    unsigned long params_len[2] = {19UL, 19UL};
    unsigned long long id = 0ULL, contact_id=0ULL;
    unsigned int count = 0U;
    int rc, okay = 1;
    (void)ctx;
    if (used) *used = 0U;
    if (!in || request_size != sizeof *in || !out || response_size < sizeof *out ||
        !memchr(in->account,0,sizeof in->account) ||
        !memchr(in->from_date,0,sizeof in->from_date) ||
        !memchr(in->until_date,0,sizeof in->until_date) ||
        !memchr(in->from_time,0,sizeof in->from_time) ||
        !sparkles_mariadb_valid_range(in->from_date,in->until_date))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    if (!calendar_contact_allowed(in->account))
        return STNLABZ_MODULE_ERR_NOT_AUTHORIZED;
    (void)snprintf(from,sizeof from,"%s 00:00:00",in->from_date);
    if(in->from_time[0]) {
        size_t j;
        static const size_t digits[]={11U,12U,14U,15U,17U,18U};
        if(strlen(in->from_time)!=19U ||
           memcmp(in->from_time,in->from_date,10U) ||
           in->from_time[10U]!=' ' || in->from_time[13U]!=':' ||
           in->from_time[16U]!=':')
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
        for(j=0U;j<sizeof digits/sizeof digits[0];++j) {
            unsigned char digit=(unsigned char)in->from_time[digits[j]];
            if(digit<'0'||digit>'9')return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
        }
        if(in->from_time[11U]>'2' ||
           (in->from_time[11U]=='2' && in->from_time[12U]>'3') ||
           in->from_time[14U]>'5' || in->from_time[17U]>'5')
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
        memcpy(from,in->from_time,20U);
    }
    (void)snprintf(until,sizeof until,"%s 00:00:00",in->until_date);
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = until;
    params[0].buffer_length = 19U;
    params[0].length = &params_len[0];
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = from;
    params[1].buffer_length = 19U;
    params[1].length = &params_len[1];

    params[2].buffer_type = MYSQL_TYPE_LONGLONG;
    params[2].buffer = &contact_id;
    params[2].is_unsigned = 1;
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = title;
    result[1].buffer_length = sizeof title;
    result[1].length = &title_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = start;
    result[2].buffer_length = sizeof start;
    result[2].length = &start_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = end;
    result[3].buffer_length = sizeof end;
    result[3].length = &end_len;

    (void)pthread_mutex_lock(&db_lock);
    if (!db || mysql_ping(db) ||
        !sparkles_mariadb_participants_supported(db) ||
        !sparkles_mariadb_contact_id(db,in->account,&contact_id))okay=0;
    if (okay) stmt = mysql_stmt_init(db);
    if (!stmt) okay = 0;
    if (okay && mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql))) okay = 0;
    if (okay && mysql_stmt_bind_param(stmt,params)) okay = 0;
    if (okay && mysql_stmt_execute(stmt)) okay = 0;
    if (okay && mysql_stmt_bind_result(stmt,result)) okay = 0;
    if (okay && mysql_stmt_store_result(stmt)) okay = 0;
    while (okay) {
        rc = mysql_stmt_fetch(stmt);
        if (rc == MYSQL_NO_DATA) break;
        if (rc != 0 || title_len >= sizeof title ||
            start_len != 19U || end_len != 19U) {
            okay = 0;
            break;
        }
        if (count == SPARKLES_MARIADB_APPOINTMENTS_MAX) {
            out->has_more = 1U;
            break;
        }
        out->records[count].id = id;
        /* Never leak a partially cut title as if it were complete. */
        if (title_len >= sizeof out->records[count].title) {
            okay = 0;
            break;
        }
        memcpy(out->records[count].title,title,title_len);
        out->records[count].title[title_len] = '\0';
        memcpy(out->records[count].start_time,start,19U);
        out->records[count].start_time[19] = '\0';
        memcpy(out->records[count].end_time,end,19U);
        out->records[count].end_time[19] = '\0';
        ++count;
    }
    if (stmt) mysql_stmt_close(stmt);
    (void)pthread_mutex_unlock(&db_lock);
    if (!okay) {
        memset(out,0,sizeof *out);
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }
    out->count = count;
    if (used) *used = sizeof *out;
    return STNLABZ_MODULE_OK;
}

static int safe_irc_title(const char *value)
{
    const unsigned char *p=(const unsigned char *)value;
    if (!value) return 0;
    for (;*p;++p) if (*p < 0x20U || *p == 0x7fU) return 0;
    return 1;
}

/* Explicit, authenticated IRC schedule lookup. No model-generated SQL or writes.
 * Read results are sent privately to the requesting nick, never to a channel. */
static stnlabz_module_result_t schedule_command(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context, void *context)
{
    sparkles_mariadb_appointments_request_t request = {0};
    sparkles_mariadb_appointments_reply_t result = {0};
    char extra;
    char text[512];
    size_t used=0U, i;
    stnlabz_module_result_t rc;
    (void)context;
    if (!command || !reply ||
        !memchr(command->arguments,0,sizeof command->arguments) ||
        !memchr(command->account,0,sizeof command->account) ||
        !memchr(command->sender,0,sizeof command->sender))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    /* A nickname is only a delivery address, never the authority source. */
    if (!calendar_contact_allowed(command->account))
        return reply(reply_context,"Schedule access denied.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    /* Exact explicit dates: no implicit day, timezone or free-text parsing. */
    if (sscanf(command->arguments,"%10s %10s %c",
               request.from_date,request.until_date,&extra)!=2 ||
        !sparkles_mariadb_valid_range(request.from_date,request.until_date))
        return reply(reply_context,
          "Usage: /schedule YYYY-MM-DD YYYY-MM-DD (end date exclusive)")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    memcpy(request.account,command->account,strlen(command->account)+1U);
    rc=appointments_service(&request,sizeof request,&result,sizeof result,&used,NULL);
    if (rc!=STNLABZ_MODULE_OK || used!=sizeof result)
        return reply(reply_context,"Schedule lookup unavailable.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    /* Private response belongs to the originating Core transport, never IRC. */
    if(!result.count)
        return reply(reply_context,
            "No scheduled appointments stored for that date range.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    {
        size_t written=0U;
        unsigned int shown=0U;
        int n=snprintf(text,sizeof text,
            "Schedule from %s:",request.from_date);
        if(n<0||(size_t)n>=sizeof text)
            return STNLABZ_MODULE_ERR_INVALID_STATE;
        written=(size_t)n;
        for(i=0U;i<result.count;++i) {
            const sparkles_mariadb_appointment_t *e=&result.records[i];
            if(!safe_irc_title(e->title))
                return STNLABZ_MODULE_ERR_INVALID_STATE;
            n=snprintf(text+written,sizeof text-written,
                "\n\n- %.5s - %.5s  %s",
                e->start_time+11U,e->end_time+11U,e->title);
            if(n<0)return STNLABZ_MODULE_ERR_INVALID_STATE;
            if((size_t)n>=sizeof text-written) {
                text[written]='\0';
                break;
            }
            written+=(size_t)n;
            ++shown;
        }
        if(!shown)return STNLABZ_MODULE_ERR_INVALID_STATE;
        if(shown<result.count||result.has_more) {
            static const char note[]=" (More appointments; narrow the range.)";
            size_t tail=sizeof note-1U;
            if(written+tail<sizeof text)
                memcpy(text+written,note,sizeof note);
        }
        return reply(reply_context,text)
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    }
}

typedef enum {
    APPOINTMENT_DB_ERROR=0,
    APPOINTMENT_DB_CONFLICT,
    APPOINTMENT_DB_SAVED,
    APPOINTMENT_DB_UNCERTAIN
} appointment_write_result_t;

/* Single-process serialization plus a contact-scoped transactional overlap
 * check. Appointments and their creator relationship are committed together.
 * External database clients must preserve the same scheduling discipline. */
static appointment_write_result_t insert_appointment(
    const sparkles_mariadb_new_appointment_t *appointment,
    const char *account,const char *guest_name,
    unsigned int public_show,const char *venue,const char *dj,
    unsigned long long *created_id)
{
    static const char overlap_sql[] =
        "SELECT a.id FROM appointments a "
        "WHERE a.status='scheduled' AND a.start_time < ? AND a.end_time > ? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=a.id AND ap.contact_id IN (?,?)) "
        "LIMIT 1 FOR UPDATE";
    static const char insert_sql[] =
        "INSERT INTO appointments (title,start_time,end_time,status) "
        "VALUES (?,?,?,'scheduled')";
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND params[4]={{0}};
    unsigned long lengths[3];
    unsigned long long contact_id=0ULL,guest_id=0ULL;
    appointment_write_result_t outcome=APPOINTMENT_DB_ERROR;
    int transaction_started=0;
    if (!appointment||!account||!created_id) return APPOINTMENT_DB_ERROR;
    *created_id=0ULL;
    lengths[0]=(unsigned long)strlen(appointment->title);
    lengths[1]=(unsigned long)strlen(appointment->start_time);
    lengths[2]=(unsigned long)strlen(appointment->end_time);
    (void)pthread_mutex_lock(&db_lock);
    if (!db || mysql_ping(db) || mysql_query(db,"START TRANSACTION")) goto done;
    transaction_started=1;
    if(!sparkles_mariadb_participants_supported(db) ||
       !sparkles_mariadb_contact_id(db,account,&contact_id))goto done;
    guest_id=contact_id;
    if(guest_name && guest_name[0] &&
       (!sparkles_mariadb_guest_id(db,guest_name,&guest_id)||
        guest_id==contact_id))goto done;

    stmt=mysql_stmt_init(db);
    if (!stmt || mysql_stmt_prepare(stmt,overlap_sql,
                                   (unsigned long)strlen(overlap_sql))) goto done;
    params[0].buffer_type=MYSQL_TYPE_STRING;
    params[0].buffer=(void *)appointment->end_time;
    params[0].buffer_length=lengths[2];
    params[0].length=&lengths[2];
    params[1].buffer_type=MYSQL_TYPE_STRING;
    params[1].buffer=(void *)appointment->start_time;
    params[1].buffer_length=lengths[1];
    params[1].length=&lengths[1];
    params[2].buffer_type=MYSQL_TYPE_LONGLONG;
    params[2].buffer=&contact_id;params[2].is_unsigned=1;
    params[3].buffer_type=MYSQL_TYPE_LONGLONG;
    params[3].buffer=&guest_id;params[3].is_unsigned=1;
    if (mysql_stmt_bind_param(stmt,params) ||
        mysql_stmt_execute(stmt) ||
        mysql_stmt_store_result(stmt)) goto done;
    if (mysql_stmt_num_rows(stmt)>0ULL) {
        outcome=APPOINTMENT_DB_CONFLICT;
        goto done;
    }
    mysql_stmt_close(stmt);
    stmt=NULL;

    memset(params,0,sizeof params);
    stmt=mysql_stmt_init(db);
    if (!stmt || mysql_stmt_prepare(stmt,insert_sql,
                                   (unsigned long)strlen(insert_sql))) goto done;
    params[0].buffer_type=MYSQL_TYPE_STRING;
    params[0].buffer=(void *)appointment->title;
    params[0].buffer_length=lengths[0];
    params[0].length=&lengths[0];
    params[1].buffer_type=MYSQL_TYPE_STRING;
    params[1].buffer=(void *)appointment->start_time;
    params[1].buffer_length=lengths[1];
    params[1].length=&lengths[1];
    params[2].buffer_type=MYSQL_TYPE_STRING;
    params[2].buffer=(void *)appointment->end_time;
    params[2].buffer_length=lengths[2];
    params[2].length=&lengths[2];
    if (mysql_stmt_bind_param(stmt,params) ||
        mysql_stmt_execute(stmt) ||
        mysql_stmt_affected_rows(stmt)!=1ULL) goto done;
    *created_id=(unsigned long long)mysql_insert_id(db);
    if (!*created_id) goto done;
    mysql_stmt_close(stmt);
    stmt=NULL;
    /* Appointment and owner association commit atomically. */
    if(!sparkles_mariadb_participant_add(db,*created_id,contact_id))goto done;
    if(guest_id!=contact_id &&
       !sparkles_mariadb_participant_invite(db,*created_id,guest_id))goto done;
    /* An explicitly confirmed show is registered with its appointment in
     * the SAME transaction; no public event survives a failed appointment. */
    if(public_show) {
        static const char show_sql[]=
            "INSERT INTO show_events "
            "(appointment_id,owner_avatar_uuid,venue,dj_name) "
            "VALUES (?,?,?,?)";
        const char *platform=NULL,*identity=NULL;
        MYSQL_BIND show_bind[4]={{0}};
        unsigned long show_lengths[3];
        if(!venue||!venue[0]||!dj||
           !sparkles_mariadb_identity_parts(account,&platform,&identity)||
           strcmp(platform,"second_life"))goto done;
        show_lengths[0]=36UL;
        show_lengths[1]=(unsigned long)strlen(venue);
        show_lengths[2]=(unsigned long)strlen(dj);
        stmt=mysql_stmt_init(db);
        if(!stmt||mysql_stmt_prepare(stmt,show_sql,
             (unsigned long)strlen(show_sql)))goto done;
        show_bind[0].buffer_type=MYSQL_TYPE_LONGLONG;
        show_bind[0].buffer=created_id;
        show_bind[0].is_unsigned=1;
        show_bind[1].buffer_type=MYSQL_TYPE_STRING;
        show_bind[1].buffer=(void *)identity;
        show_bind[1].buffer_length=show_lengths[0];
        show_bind[1].length=&show_lengths[0];
        show_bind[2].buffer_type=MYSQL_TYPE_STRING;
        show_bind[2].buffer=(void *)venue;
        show_bind[2].buffer_length=show_lengths[1];
        show_bind[2].length=&show_lengths[1];
        show_bind[3].buffer_type=MYSQL_TYPE_STRING;
        show_bind[3].buffer=(void *)dj;
        show_bind[3].buffer_length=show_lengths[2];
        show_bind[3].length=&show_lengths[2];
        if(mysql_stmt_bind_param(stmt,show_bind)||
           mysql_stmt_execute(stmt)||
           mysql_stmt_affected_rows(stmt)!=1ULL)goto done;
        mysql_stmt_close(stmt);stmt=NULL;
    }
    /* COMMIT failures cannot conclusively establish whether a write persisted. */
    outcome=APPOINTMENT_DB_UNCERTAIN;
    if (mysql_query(db,"COMMIT")==0) {
        transaction_started=0;
        outcome=APPOINTMENT_DB_SAVED;
    }
done:
    if (stmt) mysql_stmt_close(stmt);
    if (transaction_started && mysql_query(db,"ROLLBACK")!=0)
        outcome=APPOINTMENT_DB_UNCERTAIN;
    (void)pthread_mutex_unlock(&db_lock);
    return outcome;
}

/* Verified-account read-only overlap check, before a human confirms the
 * proposed events. Repeated again transactionally during INSERT. */
static stnlabz_module_result_t check_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *ctx)
{
    static const char sql[] =
        "SELECT a.id FROM appointments a WHERE a.status='scheduled' "
        "AND a.start_time < ? AND a.end_time > ? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=a.id AND ap.contact_id IN (?,?)) LIMIT 1";
    const sparkles_mariadb_create_request_t *in=request;
    sparkles_mariadb_check_reply_t *out=response;
    sparkles_mariadb_new_appointment_t appointment;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND bind[4]={{0}};
    unsigned long long contact_id=0ULL,guest_id=0ULL;
    unsigned long lens[2];
    char source[256];
    int n,ok=1,conflict=0;
    (void)ctx;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account)||
       !memchr(in->start_time,0,sizeof in->start_time)||
       !memchr(in->end_time,0,sizeof in->end_time)||
       !memchr(in->title,0,sizeof in->title) ||
       !memchr(in->guest_name,0,sizeof in->guest_name))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if(!calendar_contact_allowed(in->account)) {
        out->status=SPARKLES_MARIADB_CHECK_DENIED;
        if(used)*used=sizeof *out;
        return STNLABZ_MODULE_OK;
    }
    if(strlen(in->start_time)!=19U||strlen(in->end_time)!=19U||
       memcmp(in->start_time,in->end_time,11U)!=0)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    n=snprintf(source,sizeof source,"%.10s %.5s %.5s %s",
               in->start_time,in->start_time+11,in->end_time+11,in->title);
    if(n<0||(size_t)n>=sizeof source||
       !sparkles_mariadb_parse_new_appointment(source,&appointment)||
       strcmp(appointment.start_time,in->start_time)||
       strcmp(appointment.end_time,in->end_time))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    lens[0]=(unsigned long)strlen(appointment.end_time);
    lens[1]=(unsigned long)strlen(appointment.start_time);
    bind[0].buffer_type=MYSQL_TYPE_STRING;
    bind[0].buffer=appointment.end_time;
    bind[0].buffer_length=lens[0];bind[0].length=&lens[0];
    bind[1].buffer_type=MYSQL_TYPE_STRING;
    bind[1].buffer=appointment.start_time;
    bind[1].buffer_length=lens[1];bind[1].length=&lens[1];
    bind[2].buffer_type=MYSQL_TYPE_LONGLONG;
    bind[2].buffer=&contact_id;bind[2].is_unsigned=1;
    bind[3].buffer_type=MYSQL_TYPE_LONGLONG;
    bind[3].buffer=&guest_id;bind[3].is_unsigned=1;
    (void)pthread_mutex_lock(&db_lock);
    if(!db||mysql_ping(db)||
       !sparkles_mariadb_participants_supported(db)||
       !sparkles_mariadb_contact_id(db,in->account,&contact_id))ok=0;
    guest_id=contact_id;
    if(ok && in->guest_name[0] &&
       (!sparkles_mariadb_guest_id(db,in->guest_name,&guest_id)||
        guest_id==contact_id))ok=0;
    if(ok)stmt=mysql_stmt_init(db);
    if(!stmt)ok=0;
    if(ok&&mysql_stmt_prepare(stmt,sql,(unsigned long)strlen(sql)))ok=0;
    if(ok&&mysql_stmt_bind_param(stmt,bind))ok=0;
    if(ok&&mysql_stmt_execute(stmt))ok=0;
    if(ok&&mysql_stmt_store_result(stmt))ok=0;
    if(ok)conflict=mysql_stmt_num_rows(stmt)>0ULL;
    if(stmt)mysql_stmt_close(stmt);
    (void)pthread_mutex_unlock(&db_lock);
    out->status=ok?(conflict?SPARKLES_MARIADB_CHECK_CONFLICT:
                             SPARKLES_MARIADB_CHECK_FREE):
                    SPARKLES_MARIADB_CHECK_ERROR;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Typed service for the conversational controller. Only explicitly confirmed
 * appointments may be submitted by its caller; database-side validation and
 * authorization are repeated and never delegated to model output. */
static stnlabz_module_result_t create_service(
    const void *request, size_t request_size,
    void *response, size_t response_size, size_t *used, void *ctx)
{
    const sparkles_mariadb_create_request_t *in=request;
    sparkles_mariadb_create_reply_t *out=response;
    sparkles_mariadb_new_appointment_t appointment;
    appointment_write_result_t outcome;
    char input[256];
    unsigned long long id=0ULL;
    int n;
    (void)ctx;
    if (used) *used=0U;
    if (!in || request_size!=sizeof *in || !out || response_size<sizeof *out ||
        !memchr(in->account,0,sizeof in->account) ||
        !memchr(in->start_time,0,sizeof in->start_time) ||
        !memchr(in->end_time,0,sizeof in->end_time) ||
        !memchr(in->title,0,sizeof in->title) ||
        !memchr(in->guest_name,0,sizeof in->guest_name) ||
        !memchr(in->show_venue,0,sizeof in->show_venue) ||
        !memchr(in->show_dj,0,sizeof in->show_dj) ||
        in->public_show>1U ||
        (in->public_show && !in->show_venue[0]))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if (!calendar_contact_allowed(in->account)) {
        out->status=SPARKLES_MARIADB_CREATE_DENIED;
        if (used) *used=sizeof *out;
        return STNLABZ_MODULE_OK;
    }
    if (strlen(in->start_time)!=19U || strlen(in->end_time)!=19U ||
        memcmp(in->start_time,in->end_time,11U)!=0 ||
        memcmp(in->start_time+16,":00",3U)!=0 ||
        memcmp(in->end_time+16,":00",3U)!=0)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    n=snprintf(input,sizeof input,"%.10s %.5s %.5s %s",
        in->start_time,in->start_time+11,in->end_time+11,in->title);
    if (n<0 || (size_t)n>=sizeof input ||
        !sparkles_mariadb_parse_new_appointment(input,&appointment) ||
        strcmp(appointment.start_time,in->start_time) ||
        strcmp(appointment.end_time,in->end_time))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    outcome=insert_appointment(&appointment,in->account,in->guest_name,
                               in->public_show,in->show_venue,in->show_dj,&id);
    out->id=outcome==APPOINTMENT_DB_SAVED?id:0ULL;
    out->status=outcome==APPOINTMENT_DB_SAVED?SPARKLES_MARIADB_CREATE_SAVED:
        outcome==APPOINTMENT_DB_CONFLICT?SPARKLES_MARIADB_CREATE_CONFLICT:
        outcome==APPOINTMENT_DB_UNCERTAIN?SPARKLES_MARIADB_CREATE_UNCERTAIN:
                                      SPARKLES_MARIADB_CREATE_ERROR;
    if (used) *used=sizeof *out;
    return STNLABZ_MODULE_OK;
}


/* Transactional optimistic mutation. The source snapshot comes from a
 * verified-account read and is checked again while the row is locked. */
static stnlabz_module_result_t mutate_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *ctx)
{
    static const char match_sql[] =
        "SELECT a.id FROM appointments a WHERE a.id=? AND a.status='scheduled' "
        "AND a.title=? AND a.start_time=? AND a.end_time=? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=a.id AND ap.contact_id=?) FOR UPDATE";
    static const char overlap_sql[] =
        "SELECT a.id FROM appointments a WHERE a.id<>? AND a.status='scheduled' "
        "AND a.start_time < ? AND a.end_time > ? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=a.id AND ap.contact_id=?) LIMIT 1 FOR UPDATE";
    static const char cancel_sql[] =
        "UPDATE appointments SET status='cancelled' WHERE id=? "
        "AND status='scheduled' AND title=? AND start_time=? AND end_time=? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=appointments.id AND ap.contact_id=?)";
    static const char update_sql[] =
        "UPDATE appointments SET title=?,start_time=?,end_time=? WHERE id=? "
        "AND status='scheduled' AND title=? AND start_time=? AND end_time=? "
        "AND EXISTS (SELECT 1 FROM appointment_participants ap "
        "WHERE ap.appointment_id=appointments.id AND ap.contact_id=?)";
    const sparkles_mariadb_mutate_request_t *in=request;
    sparkles_mariadb_mutate_reply_t *out=response;
    sparkles_mariadb_new_appointment_t validated;
    MYSQL_STMT *stmt=NULL;
    MYSQL_BIND bind[8]={{0}};
    unsigned long lengths[8]={0};
    unsigned long long id,contact_id=0ULL;
    int transaction=0,matched=0;
    char new_input[256];
    int n;
    (void)ctx;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account)||
       !memchr(in->previous_title,0,sizeof in->previous_title)||
       !memchr(in->previous_start,0,sizeof in->previous_start)||
       !memchr(in->previous_end,0,sizeof in->previous_end)||
       !memchr(in->next_title,0,sizeof in->next_title)||
       !memchr(in->next_start,0,sizeof in->next_start)||
       !memchr(in->next_end,0,sizeof in->next_end)||
       !in->id ||
       (in->operation!=SPARKLES_MARIADB_MUTATE_CANCEL &&
        in->operation!=SPARKLES_MARIADB_MUTATE_UPDATE))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if(!calendar_contact_allowed(in->account)) {
        out->status=SPARKLES_MARIADB_MUTATE_DENIED;
        if(used)*used=sizeof *out;
        return STNLABZ_MODULE_OK;
    }
    if(!in->previous_title[0]||
       strlen(in->previous_start)!=19U||
       strlen(in->previous_end)!=19U||
       !safe_irc_title(in->previous_title))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    if(in->operation==SPARKLES_MARIADB_MUTATE_UPDATE) {
        if(strlen(in->next_start)!=19U||strlen(in->next_end)!=19U||
           memcmp(in->next_start,in->next_end,11U)!=0||
           memcmp(in->next_start+16,":00",3U)!=0||
           memcmp(in->next_end+16,":00",3U)!=0)
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
        n=snprintf(new_input,sizeof new_input,"%.10s %.5s %.5s %s",
                   in->next_start,in->next_start+11,
                   in->next_end+11,in->next_title);
        if(n<0||(size_t)n>=sizeof new_input||
           !sparkles_mariadb_parse_new_appointment(new_input,&validated)||
           strcmp(validated.start_time,in->next_start)||
           strcmp(validated.end_time,in->next_end))
            return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }
    id=in->id;
    out->status=SPARKLES_MARIADB_MUTATE_ERROR;
    (void)pthread_mutex_lock(&db_lock);
    if(!db||mysql_ping(db)||mysql_query(db,"START TRANSACTION"))goto done_mutate;
    transaction=1;
    if(!sparkles_mariadb_participants_supported(db)||
       !sparkles_mariadb_contact_id(db,in->account,&contact_id))
       goto done_mutate;
    stmt=mysql_stmt_init(db);
    if(!stmt||mysql_stmt_prepare(stmt,match_sql,(unsigned long)strlen(match_sql)))
        goto done_mutate;
    bind[0].buffer_type=MYSQL_TYPE_LONGLONG;
    bind[0].buffer=&id;bind[0].is_unsigned=1;
    bind[1].buffer_type=MYSQL_TYPE_STRING;
    bind[1].buffer=(void *)in->previous_title;
    lengths[1]=(unsigned long)strlen(in->previous_title);
    bind[1].buffer_length=lengths[1];bind[1].length=&lengths[1];
    bind[2].buffer_type=MYSQL_TYPE_STRING;
    bind[2].buffer=(void *)in->previous_start;
    lengths[2]=(unsigned long)strlen(in->previous_start);
    bind[2].buffer_length=lengths[2];bind[2].length=&lengths[2];
    bind[3].buffer_type=MYSQL_TYPE_STRING;
    bind[3].buffer=(void *)in->previous_end;
    lengths[3]=(unsigned long)strlen(in->previous_end);
    bind[3].buffer_length=lengths[3];bind[3].length=&lengths[3];
    bind[4].buffer_type=MYSQL_TYPE_LONGLONG;
    bind[4].buffer=&contact_id;bind[4].is_unsigned=1;
    if(mysql_stmt_bind_param(stmt,bind)||mysql_stmt_execute(stmt)||
       mysql_stmt_store_result(stmt))goto done_mutate;
    matched=mysql_stmt_num_rows(stmt)==1ULL;
    mysql_stmt_close(stmt);stmt=NULL;
    if(!matched) {
        out->status=SPARKLES_MARIADB_MUTATE_STALE;
        goto done_mutate;
    }
    if(in->operation==SPARKLES_MARIADB_MUTATE_UPDATE) {
        memset(bind,0,sizeof bind);
        stmt=mysql_stmt_init(db);
        if(!stmt||mysql_stmt_prepare(stmt,overlap_sql,
                                    (unsigned long)strlen(overlap_sql)))
            goto done_mutate;
        bind[0].buffer_type=MYSQL_TYPE_LONGLONG;
        bind[0].buffer=&id;bind[0].is_unsigned=1;
        bind[1].buffer_type=MYSQL_TYPE_STRING;
        bind[1].buffer=(void *)in->next_end;
        lengths[1]=19U;bind[1].buffer_length=19U;bind[1].length=&lengths[1];
        bind[2].buffer_type=MYSQL_TYPE_STRING;
        bind[2].buffer=(void *)in->next_start;
        lengths[2]=19U;bind[2].buffer_length=19U;bind[2].length=&lengths[2];
        bind[3].buffer_type=MYSQL_TYPE_LONGLONG;
        bind[3].buffer=&contact_id;bind[3].is_unsigned=1;
        if(mysql_stmt_bind_param(stmt,bind)||mysql_stmt_execute(stmt)||
           mysql_stmt_store_result(stmt))goto done_mutate;
        if(mysql_stmt_num_rows(stmt)>0ULL) {
            out->status=SPARKLES_MARIADB_MUTATE_CONFLICT;
            goto done_mutate;
        }
        mysql_stmt_close(stmt);stmt=NULL;
    }
    memset(bind,0,sizeof bind);
    stmt=mysql_stmt_init(db);
    if(!stmt||mysql_stmt_prepare(stmt,
         in->operation==SPARKLES_MARIADB_MUTATE_CANCEL?cancel_sql:update_sql,
         (unsigned long)strlen(in->operation==SPARKLES_MARIADB_MUTATE_CANCEL?
                                cancel_sql:update_sql)))goto done_mutate;
    {
        unsigned int offset=0U,j;
        const char *values[6];
        if(in->operation==SPARKLES_MARIADB_MUTATE_UPDATE) {
            values[0]=in->next_title;values[1]=in->next_start;
            values[2]=in->next_end;
            for(j=0U;j<3U;++j) {
                bind[j].buffer_type=MYSQL_TYPE_STRING;
                bind[j].buffer=(void *)values[j];
                lengths[j]=(unsigned long)strlen(values[j]);
                bind[j].buffer_length=lengths[j];bind[j].length=&lengths[j];
            }
            offset=3U;
        }
        bind[offset].buffer_type=MYSQL_TYPE_LONGLONG;
        bind[offset].buffer=&id;bind[offset].is_unsigned=1;
        values[0]=in->previous_title;values[1]=in->previous_start;
        values[2]=in->previous_end;
        for(j=0U;j<3U;++j) {
            unsigned int k=offset+j+1U;
            bind[k].buffer_type=MYSQL_TYPE_STRING;
            bind[k].buffer=(void *)values[j];
            lengths[k]=(unsigned long)strlen(values[j]);
            bind[k].buffer_length=lengths[k];bind[k].length=&lengths[k];
        }
        bind[offset+4U].buffer_type=MYSQL_TYPE_LONGLONG;
        bind[offset+4U].buffer=&contact_id;bind[offset+4U].is_unsigned=1;
    }
    if(mysql_stmt_bind_param(stmt,bind)||mysql_stmt_execute(stmt)||
       mysql_stmt_affected_rows(stmt)!=1ULL)goto done_mutate;
    mysql_stmt_close(stmt);stmt=NULL;
    out->status=SPARKLES_MARIADB_MUTATE_UNCERTAIN;
    if(mysql_query(db,"COMMIT")==0) {
        transaction=0;
        out->status=SPARKLES_MARIADB_MUTATE_APPLIED;
    }
done_mutate:
    if(stmt)mysql_stmt_close(stmt);
    if(transaction && mysql_query(db,"ROLLBACK")!=0)
        out->status=SPARKLES_MARIADB_MUTATE_UNCERTAIN;
    (void)pthread_mutex_unlock(&db_lock);
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Authorized human-directed insert, never an Ollama inference side effect. */
static stnlabz_module_result_t appointment_add_command(
    const stnlabz_module_command_t *command,
    stnlabz_module_command_reply_fn reply,
    void *reply_context, void *context)
{
    sparkles_mariadb_new_appointment_t appointment;
    unsigned long long id=0ULL;
    appointment_write_result_t outcome;
    char confirmation[320];
    int n;
    (void)context;
    if (!command || !reply ||
        !memchr(command->arguments,0,sizeof command->arguments) ||
        !memchr(command->account,0,sizeof command->account) ||
        !memchr(command->sender,0,sizeof command->sender))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    if (!calendar_contact_allowed(command->account))
        return reply(reply_context,"Appointment access denied.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    if (!sparkles_mariadb_parse_new_appointment(command->arguments,&appointment))
        return reply(reply_context,
          "Usage: /appointment-add YYYY-MM-DD HH:MM HH:MM Title (same day)")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    outcome=insert_appointment(&appointment,command->account,NULL,0U,NULL,NULL,&id);
    if (outcome==APPOINTMENT_DB_CONFLICT)
        return reply(reply_context,
          "Appointment not saved: conflicts with an existing scheduled appointment.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    if (outcome==APPOINTMENT_DB_UNCERTAIN)
        return reply(reply_context,
          "Database write uncertain. Check /schedule before retrying.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    if (outcome!=APPOINTMENT_DB_SAVED)
        return reply(reply_context,"Appointment not saved: database error.")
            ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
    n=snprintf(confirmation,sizeof confirmation,
        "Appointment #%llu saved: %s to %s: %s",
        id,appointment.start_time,appointment.end_time,appointment.title);
    if (n<0 || (size_t)n>=sizeof confirmation)
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    return reply(reply_context,confirmation)
        ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_INVALID_STATE;
}

/* Account-separated durable observations. A nickname is never an identity.
 * No inferred memories are silently promoted to verified facts. */
static stnlabz_module_result_t memory_exchange_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *context)
{
    const sparkles_memory_exchange_request_t *in=request;
    sparkles_memory_exchange_reply_t *out=response;
    int ok=0;
    (void)context;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out||
       !sparkles_mariadb_memory_valid(in))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if(db && !mysql_ping(db))
        ok=sparkles_mariadb_context_exchange(db,in,out);
    (void)pthread_mutex_unlock(&db_lock);
    if(!ok)return STNLABZ_MODULE_ERR_INVALID_STATE;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

/* Contact discovery is scoped to a simulator-issued SL UUID.
 * Contact identity is NOT an authorization to read or mutate a calendar. */
static stnlabz_module_result_t contacts_service(
    const void *request,size_t request_size,
    void *response,size_t response_size,size_t *used,void *context)
{
    const sparkles_contact_request_t *in=request;
    sparkles_contact_reply_t *out=response;
    int ok=0;
    (void)context;
    if(used)*used=0U;
    if(!in||request_size!=sizeof *in||!out||response_size<sizeof *out||
       !memchr(in->account,0,sizeof in->account) ||
       !memchr(in->avatar_name,0,sizeof in->avatar_name) ||
       !memchr(in->first_name,0,sizeof in->first_name))
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    (void)pthread_mutex_lock(&db_lock);
    if(db && !mysql_ping(db))
        ok=sparkles_mariadb_contact_identify(db,in,out);
    (void)pthread_mutex_unlock(&db_lock);
    if(!ok)return STNLABZ_MODULE_ERR_INVALID_STATE;
    if(used)*used=sizeof *out;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t qualify(stnlabz_module_qualification_result_t *q)
{
    unsigned int e = 0U, p = 0U;
    int ne = 0, np = 0;
    int ok;
    unsigned int date_e=0U, date_p=0U;
    unsigned int appointment_e=0U, appointment_p=0U;
    unsigned int memory_e=0U, memory_p=0U;
    if (!q) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(q, 0, sizeof *q);
    ok = sparkles_mariadb_selftests(&e, &p, &ne, &np);
    ok = sparkles_mariadb_date_selftests(&date_e,&date_p) && ok;
    e += date_e;
    p += date_p;
    ok = sparkles_mariadb_appointment_selftests(&appointment_e,&appointment_p) && ok;
    e += appointment_e;
    p += appointment_p;
    ok = sparkles_mariadb_memory_selftests(&memory_e,&memory_p) && ok;
    e += memory_e;
    p += memory_p;
    /* No live database needed to validate fail-closed group service
     * boundaries. Live SQL joins require a separate operator test. */
    {
        sparkles_sl_group_resolve_request_t request={0};
        sparkles_sl_group_resolve_reply_t response={0};
        size_t used=9U;
        stnlabz_module_result_t rc;
        ++e;++ne;
        rc=group_resolve_service(&request,sizeof request,&response,
                                 sizeof response,&used,NULL);
        if(rc==STNLABZ_MODULE_ERR_INVALID_ARGUMENT && used==0U)
            {++p;++np;} else ok=0;
        memcpy(request.account,
               "sl:12345678-1234-1234-1234-123456789abc",40U);
        memcpy(request.group_name,"STN-Labz",9U);
        used=9U;
        ++e;++ne;
        rc=group_resolve_service(&request,sizeof request,&response,
                                 sizeof response,&used,NULL);
        if(rc==STNLABZ_MODULE_ERR_INVALID_STATE && used==0U &&
           !response.matched){++p;++np;} else ok=0;
        request.notice=2U;
        used=9U;
        ++e;++ne;
        rc=group_resolve_service(&request,sizeof request,&response,
                                 sizeof response,&used,NULL);
        if(rc==STNLABZ_MODULE_ERR_INVALID_ARGUMENT && used==0U &&
           !response.matched){++p;++np;} else ok=0;
        request.notice=1U;
        used=9U;
        ++e;++ne;
        rc=group_resolve_service(&request,sizeof request,&response,
                                 sizeof response,&used,NULL);
        if(rc==STNLABZ_MODULE_ERR_INVALID_STATE && used==0U &&
           !response.matched){++p;++np;} else ok=0;
        request.notice=0U;
        memcpy(request.account,"irc:unverified",15U);
        used=9U;
        ++e;++ne;
        rc=group_resolve_service(&request,sizeof request,&response,
                                 sizeof response,&used,NULL);
        if(rc==STNLABZ_MODULE_ERR_NOT_AUTHORIZED && used==0U &&
           !response.matched){++p;++np;} else ok=0;
    }
    q->tests_executed = e;
    q->tests_passed = p;
    q->tests_failed = e - p;
    q->negative_test_executed = ne;
    q->negative_test_passed = np;
    return ok ? STNLABZ_MODULE_OK : STNLABZ_MODULE_ERR_QUALIFICATION;
}

static stnlabz_module_result_t stop(void);
static stnlabz_module_result_t start(const stnlabz_module_host_t *h)
{
    sparkles_mariadb_config_t config;
    MYSQL *candidate;
    unsigned int timeout = 3U;
    if (!h || !h->register_service || !h->unregister_service ||
        !h->register_command || !h->unregister_command ||
        !h->send_private_message)
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    if (!sparkles_mariadb_load_config(SPARKLES_MARIADB_CONFIG, &config)) {
        fprintf(stderr, "[MARIADB] credential file missing or invalid\n");
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    candidate = mysql_init(NULL);
    if (!candidate) {
        memset(&config, 0, sizeof config);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    (void)mysql_options(candidate, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    (void)mysql_options(candidate, MYSQL_OPT_READ_TIMEOUT, &timeout);
    if (!mysql_real_connect(candidate, SPARKLES_MARIADB_HOST,
            SPARKLES_MARIADB_USER, config.password,
            SPARKLES_MARIADB_DATABASE, 0U, NULL, 0UL)) {
        fprintf(stderr, "[MARIADB] local database connection failed\n");
        memset(&config, 0, sizeof config);
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    memset(&config, 0, sizeof config);
    if (schema_table_count(candidate) != SPARKLES_MARIADB_EXPECTED_TABLES) {
        fprintf(stderr, "[MARIADB] expected seven tables not found\n");
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (!h->register_service(SPARKLES_MARIADB_STATUS_SERVICE,
                             status_service, NULL)) {
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    db = candidate;
    if (!h->register_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,
                             appointments_service, NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE, NULL);
        db = NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    if (!h->register_service(SPARKLES_MARIADB_CREATE_SERVICE,
                             create_service,NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    create_service_registered=1;
    if (!h->register_service(SPARKLES_MARIADB_CHECK_SERVICE,
                             check_service,NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    check_service_registered=1;
    if(!h->register_service(SPARKLES_MARIADB_MUTATE_SERVICE,mutate_service,NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        db=NULL;mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    mutate_service_registered=1;
    host_owner = h;
    registered = 1;
    if (!h->register_command("schedule",schedule_command,NULL)) {
        (void)h->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL);
        mutate_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        registered=0;
        host_owner=NULL;
        db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    command_registered=1;
    if (!h->register_command("appointment-add",appointment_add_command,NULL)) {
        (void)h->unregister_command("schedule",NULL);
        command_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL);
        mutate_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        registered=0;
        host_owner=NULL;
        db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    add_command_registered=1;
    if (!h->register_service(SPARKLES_MEMORY_EXCHANGE_SERVICE,
                             memory_exchange_service,NULL)) {
        (void)h->unregister_command("appointment-add",NULL);
        add_command_registered=0;
        (void)h->unregister_command("schedule",NULL);
        command_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL);
        mutate_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        registered=0;
        host_owner=NULL;
        db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    memory_service_registered=1;
    if(!h->register_service(SPARKLES_CONTACTS_SERVICE,contacts_service,NULL)) {
        (void)h->unregister_service(SPARKLES_MEMORY_EXCHANGE_SERVICE,NULL);
        memory_service_registered=0;
        (void)h->unregister_command("appointment-add",NULL);
        add_command_registered=0;
        (void)h->unregister_command("schedule",NULL);
        command_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL);
        mutate_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        registered=0;host_owner=NULL;db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    contacts_service_registered=1;
    /* Existing calendar/contact services stay available even if the
     * operator-created optional group tables are not yet populated. */
    if(!h->register_service(SPARKLES_SL_GROUP_RESOLVE_SERVICE,
                            group_resolve_service,NULL)) {
        (void)h->unregister_service(SPARKLES_CONTACTS_SERVICE,NULL);
        contacts_service_registered=0;
        (void)h->unregister_service(SPARKLES_MEMORY_EXCHANGE_SERVICE,NULL);
        memory_service_registered=0;
        (void)h->unregister_command("appointment-add",NULL);
        add_command_registered=0;
        (void)h->unregister_command("schedule",NULL);
        command_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL);
        mutate_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL);
        check_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL);
        create_service_registered=0;
        (void)h->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE,NULL);
        registered=0;host_owner=NULL;db=NULL;
        mysql_close(candidate);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    group_resolve_registered=1;
    if(!h->register_service(SPARKLES_SL_MARKETING_GROUPS_SERVICE,
                            marketing_groups_service,NULL)) {
        (void)stop();
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    marketing_groups_registered=1;
    if(!h->register_service(SPARKLES_SHOW_CLAIM_SERVICE,shows_claim_service,NULL) ||
       !h->register_service(SPARKLES_SHOW_RESPONSE_SERVICE,shows_response_service,NULL) ||
       !h->register_service(SPARKLES_SHOW_ATTEMPT_SERVICE,shows_attempt_service,NULL)) {
        /* Registration may have partially completed. Undo in reverse order. */
        (void)h->unregister_service(SPARKLES_SHOW_ATTEMPT_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_SHOW_RESPONSE_SERVICE,NULL);
        (void)h->unregister_service(SPARKLES_SHOW_CLAIM_SERVICE,NULL);
        /* Core services must not be torn down with an active connection.
         * Fail module start and unwind using the same stop path. */
        {
            (void)stop();
        }
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    show_claim_registered=show_response_registered=show_attempt_registered=1;
    fprintf(stderr, "[MARIADB] local database connected; seven core tables verified\n");
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t stop(void)
{
    if(show_attempt_registered && host_owner &&
       !host_owner->unregister_service(SPARKLES_SHOW_ATTEMPT_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    show_attempt_registered=0;
    if(show_response_registered && host_owner &&
       !host_owner->unregister_service(SPARKLES_SHOW_RESPONSE_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    show_response_registered=0;
    if(show_claim_registered && host_owner &&
       !host_owner->unregister_service(SPARKLES_SHOW_CLAIM_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    show_claim_registered=0;
    if(marketing_groups_registered && host_owner &&
       !host_owner->unregister_service(SPARKLES_SL_MARKETING_GROUPS_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    marketing_groups_registered=0;
    if (group_resolve_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_SL_GROUP_RESOLVE_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    group_resolve_registered=0;
    if (contacts_service_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_CONTACTS_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    contacts_service_registered=0;
    if (memory_service_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MEMORY_EXCHANGE_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    memory_service_registered=0;
    if (add_command_registered && host_owner &&
        !host_owner->unregister_command("appointment-add",NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    add_command_registered=0;
    if (command_registered && host_owner &&
        !host_owner->unregister_command("schedule",NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    command_registered=0;
    if (registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_STATUS_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    if (registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_APPOINTMENTS_SERVICE, NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    if (mutate_service_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_MUTATE_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    mutate_service_registered=0;
    if (check_service_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_CHECK_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    check_service_registered=0;
    if (create_service_registered && host_owner &&
        !host_owner->unregister_service(SPARKLES_MARIADB_CREATE_SERVICE,NULL))
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    create_service_registered=0;
    registered = 0;
    host_owner = NULL;
    (void)pthread_mutex_lock(&db_lock);
    if (db) mysql_close(db);
    (void)pthread_mutex_unlock(&db_lock);
    db = NULL;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t descriptor = {
    "mariadb", "Sparkles MariaDB Context Foundation", 1U, 10U, 4U,
    STNLABZ_MODULE_API_MAJOR, STNLABZ_MODULE_API_MINOR,
    qualify, start, stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &descriptor;
}
