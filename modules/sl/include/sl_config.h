#ifndef SPARKLES_SL_CONFIG_H
#define SPARKLES_SL_CONFIG_H
#include "sl_login.h"

/* Operator-selected private native C configuration file. No shell eval. */
#define SPARKLES_SL_CONF_PATH "/opt/sparkles/auth/sl/sl.conf"

/* Only an absolute, regular, UID-owned 0600 file is accepted.
 * The caller must cleanse the struct after using its password. */
int sparkles_sl_config_load(const char *path, sparkles_sl_login_config_t *out);
void sparkles_sl_config_clear(sparkles_sl_login_config_t *config);
#endif
