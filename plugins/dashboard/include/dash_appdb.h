#pragma once

/* Optional logger for diagnostics. */
void dash_appdb_set_logger(void (*fn)(const char *msg));

/* Sets visible=0/1 for a title id in app.db. Returns 0 on success, 1 if the
 * tile is not present, -1 on error. *changed_out receives the rows changed. */
int dash_appdb_set_tile(const char *title_id, int visible, int *changed_out);
