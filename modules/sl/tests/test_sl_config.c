#define _POSIX_C_SOURCE 200809L
#include "sl_config.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_content(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(content, f) >= 0);
    assert(fclose(f) == 0);
}
int main(void)
{
    char path[] = "/tmp/sparkles-sl-config-XXXXXX";
    char alternate[128];
    sparkles_sl_login_config_t cfg = {0};
    int fd = mkstemp(path);
    const char *good =
        "# SL test credentials are intentionally fictitious\n"
        "first=MsSparkles\nlast=Resident\npassword=test-only-not-real\n"
        "start=home\n";
    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(chmod(path, 0600) == 0);
    write_content(path, good);
    assert(sparkles_sl_config_load(path, &cfg));
    assert(!strcmp(cfg.first, "MsSparkles"));
    assert(!strcmp(cfg.password, "test-only-not-real"));
    sparkles_sl_config_clear(&cfg);
    assert(cfg.password[0] == '\0');
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\ngroup_chat=12345678-1234-1234-1234-123456789abc\n");
    assert(sparkles_sl_config_load(path,&cfg));
    assert(!strcmp(cfg.group_chat,"12345678-1234-1234-1234-123456789abc"));
    sparkles_sl_config_clear(&cfg);
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\ngroup_chat=12345678-1234-1234-1234-123456789abc\n"
        "group_name=My Group\n"
        "group_chat_2=22345678-1234-1234-1234-123456789abc\n"
        "group_name_2=STN-Labz\n"
        "operator_uuid=32345678-1234-1234-1234-123456789abc\n");
    assert(sparkles_sl_config_load(path,&cfg));
    assert(!strcmp(cfg.group_name,"My Group"));
    assert(!strcmp(cfg.group_name_2,"STN-Labz"));
    assert(!strcmp(cfg.operator_uuid,"32345678-1234-1234-1234-123456789abc"));
    sparkles_sl_config_clear(&cfg);
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\noperator_uuid=invalid\n");
    assert(!sparkles_sl_config_load(path,&cfg));
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\ngroup_chat_2=22345678-1234-1234-1234-123456789abc\n");
    assert(!sparkles_sl_config_load(path,&cfg));
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\ngroup_chat=not-a-uuid\n");
    assert(!sparkles_sl_config_load(path,&cfg));
    write_content(path,good);
    assert(chmod(path, 0644) == 0);
    assert(!sparkles_sl_config_load(path, &cfg));
    assert(chmod(path, 0600) == 0);
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\npassword=second\n");
    assert(!sparkles_sl_config_load(path, &cfg));
    write_content(path,
        "first=MsSparkles\nlast=Resident\npassword=test\n"
        "start=home\nunknown=value\n");
    assert(!sparkles_sl_config_load(path, &cfg));
    write_content(path, good);
    assert(snprintf(alternate, sizeof alternate, "%s.link", path) > 0);
    assert(symlink(path, alternate) == 0);
    assert(!sparkles_sl_config_load(alternate, &cfg));
    assert(unlink(alternate) == 0);
    assert(unlink(path) == 0);
    puts("SL secure configuration regression: PASS");
    return 0;
}
