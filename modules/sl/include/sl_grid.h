#ifndef SPARKLES_SL_GRID_H
#define SPARKLES_SL_GRID_H
#include <stdatomic.h>

/* Blocking native-C grid worker. Returns only after stop was requested.
 * Establishing avatar presence does not yet provide IM/group transport. */
void sparkles_sl_grid_run(atomic_int *stop);
#endif
