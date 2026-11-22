#pragma once

/* Starts the Welcome Hub bridge in a background thread (idempotent).
 * Returns 0 on success, -1 if the thread could not be created. */
int wh_runner_start(void);
