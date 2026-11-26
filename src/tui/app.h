#ifndef STEAMSCRIBE_TUI_APP_H
#define STEAMSCRIBE_TUI_APP_H

#include <stdint.h>

#include "config.h"
#include "util/arena.h"
#include "util/error.h"

typedef struct tui_options_t
{
    const char *initial_query; /* may be NULL/empty; pre-fills the Search screen */
    uint32_t initial_appid;    /* nonzero skips Search/Select entirely (the --appid path) */
} tui_options_t;

/* Runs the full-screen TUI to completion (until the user quits). `cfg` may
 * be updated and (on Ctrl+S) saved to disk by the in-TUI Settings screen
 * (F2) -- not const because of that. Returns 0 on a normal quit, -1 if
 * termbox2 failed to attach to a terminal (`err` is filled in for that case). */
int tui_run(arena_t *arena, config_t *cfg, const tui_options_t *options, err_t *err);

#endif
