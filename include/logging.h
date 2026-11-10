#ifndef STNLABZ_LOGGING_H
#define STNLABZ_LOGGING_H

#define STNLABZ_INSTALL_PATH "/opt/sparkles"
#define STNLABZ_LOG_DIRECTORY "./logs"
#define STNLABZ_LOG_PATH "./logs/sparkles.log"
#define STNLABZ_LEGACY_MODULE_LOG_PATH "./logs/modules.log"

int stnlabz_log_open(void);
int stnlabz_log_open_path(const char *absolute_path); /* tests only */
int stnlabz_log_event(const char *component, const char *event, const char *detail);
int stnlabz_log_sync(void);
void stnlabz_log_close(void);
int stnlabz_log_is_open(void);

#endif
