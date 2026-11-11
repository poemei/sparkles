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
