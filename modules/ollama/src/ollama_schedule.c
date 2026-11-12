#include <stdio.h>
#include <string.h>
#include "ollama_schedule.h"

static int valid_date(const char *s)
{
    unsigned y,m,d,max;
    static const unsigned days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned i;
    if(!s||strlen(s)!=10U||s[4]!='-'||s[7]!='-')return 0;
    for(i=0;i<10U;++i)if(i!=4U&&i!=7U&&(s[i]<'0'||s[i]>'9'))return 0;
    y=(unsigned)(s[0]-'0')*1000U+(unsigned)(s[1]-'0')*100U+
      (unsigned)(s[2]-'0')*10U+(unsigned)(s[3]-'0');
    m=(unsigned)(s[5]-'0')*10U+(unsigned)(s[6]-'0');
    d=(unsigned)(s[8]-'0')*10U+(unsigned)(s[9]-'0');
    if(y<2000U||m==0U||m>12U||!d)return 0;
    max=days[m-1U]+(m==2U&&y%4U==0U&&(y%100U!=0U||y%400U==0U));
    return d<=max;
}
static int valid_time(const char *s)
{
    unsigned hh,mm;
    if(!s||strlen(s)!=5U||s[2]!=':'||
       s[0]<'0'||s[0]>'9'||s[1]<'0'||s[1]>'9'||
       s[3]<'0'||s[3]>'9'||s[4]<'0'||s[4]>'9')return 0;
    hh=(unsigned)(s[0]-'0')*10U+(unsigned)(s[1]-'0');
    mm=(unsigned)(s[3]-'0')*10U+(unsigned)(s[4]-'0');
    return hh<24U&&mm<60U;
}
int sparkles_schedule_date(const char *text,char date[11])
{
    size_t n;
    if(!text||!date)return 0;
    n=strlen(text);
    if(n!=10U||!valid_date(text))return 0;
    memcpy(date,text,11U);
    return 1;
}
int sparkles_schedule_parse(const char *output,sparkles_schedule_plan_t *plan)
{
    const char *p;
    if(!output||!plan)return 0;
    memset(plan,0,sizeof *plan);
    if(!strcmp(output,"NONE"))return 1;
    p=output;
    while(*p) {
        const char *sep[4],*end;
        sparkles_schedule_item_t *item;
        size_t lens[4],i;
        if(plan->count>=SPARKLES_SCHEDULE_ITEMS_MAX||
           strncmp(p,"EVENT|",6U))goto fail;
        p+=6U;
        for(i=0U;i<3U;++i) {
            sep[i]=strchr(p,'|');
            if(!sep[i])goto fail;
            lens[i]=(size_t)(sep[i]-p);
            p=sep[i]+1;
        }
        end=strchr(p,'\n');
        if(!end)end=p+strlen(p);
        lens[3]=(size_t)(end-p);
        if((lens[0]!=10U&&lens[0]!=7U)||
           lens[1]!=5U||(lens[2]!=5U&&lens[2]!=7U)||
           !lens[3]||lens[3]>200U)goto fail;
        /* Recover field starts from delimiters. */
        item=&plan->items[plan->count];
        memcpy(item->date,sep[0]-lens[0],lens[0]);
        memcpy(item->begin,sep[1]-lens[1],lens[1]);
        memcpy(item->end,sep[2]-lens[2],lens[2]);
        memcpy(item->title,p,lens[3]);
        item->date[lens[0]]='\0';
        item->begin[5]='\0';
        item->end[lens[2]]='\0';
        item->title[lens[3]]='\0';
        if((strcmp(item->date,"UNKNOWN")&& !valid_date(item->date))||
           !valid_time(item->begin)||
           (strcmp(item->end,"UNKNOWN")&&
            (!valid_time(item->end)||strcmp(item->begin,item->end)>=0)))
            goto fail;
        for(i=0U;i<lens[3];++i) {
            unsigned char c=(unsigned char)item->title[i];
            if(c<0x20U||c==0x7fU||c=='|')goto fail;
        }
        if(item->title[0]==' '||item->title[lens[3]-1U]==' ')goto fail;
        ++plan->count;
        if(!*end)break;
        p=end+1;
        if(!*p)break;
    }
    return plan->count>0U;
fail:
    memset(plan,0,sizeof *plan);
    return 0;
}
int sparkles_schedule_selftests(unsigned int *executed,unsigned int *passed)
{
    sparkles_schedule_plan_t p;
    char date[11];
    unsigned int n=0U,ok=0U;
#define T(x) do {++n; if(x)++ok;}while(0)
    T(sparkles_schedule_parse("NONE",&p)&&p.count==0U);
    T(sparkles_schedule_parse("EVENT|UNKNOWN|14:00|16:00|The River - Rachel\nEVENT|UNKNOWN|18:00|20:00|Shenanigans - Katy",&p)&&p.count==2U);
    T(!strcmp(p.items[1].title,"Shenanigans - Katy"));
    T(sparkles_schedule_parse("EVENT|2026-11-11|14:00|16:00|The River",&p)&&p.count==1U);
    T(sparkles_schedule_parse("EVENT|UNKNOWN|16:30|UNKNOWN|River management",&p) &&
      p.count==1U && !strcmp(p.items[0].end,"UNKNOWN"));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|16:30|INVALID|River management",&p));
    T(!sparkles_schedule_parse("EVENT|2026-02-30|14:00|16:00|Invalid",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|25:00|16:00|Invalid",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|16:00|14:00|Invalid",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|14:00|14:00|Invalid",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|14:00|16:00|Title|Extra",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|14:00|16:00|A\nTHIRD",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|14:00|16:00|Bad\rTitle",&p));
    T(!sparkles_schedule_parse("EVENT|UNKNOWN|14:00|16:00|",&p));
    T(!sparkles_schedule_parse("garbage",&p));
    T(!sparkles_schedule_parse(NULL,&p));
    T(!sparkles_schedule_parse("NONE",NULL));
    T(sparkles_schedule_date("2026-11-11",date)&&!strcmp(date,"2026-11-11"));
    T(!sparkles_schedule_date("tomorrow",date));
    T(!sparkles_schedule_date("2026-02-30",date));
#undef T
    if(executed)*executed=n;
    if(passed)*passed=ok;
    return n==ok;
}
