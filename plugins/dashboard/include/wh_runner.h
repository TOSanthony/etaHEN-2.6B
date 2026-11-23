#pragma once

/* Starts the Welcome Hub bridge in a background thread (idempotent). If restart_shell is set, SceShellUI is restarted once,
 * restart_delay_s seconds after the first injection, and the bridge is injected again into the new shell.
 * Returns 0 on success, -1 if the thread could not be created. */
int wh_runner_start(int restart_shell, int restart_delay_s);

/* Restarts SceShellUI now (once etaHEN is ready), then re-injects the Welcome Hub bridge. Returns 0 if started/already running. */
int wh_runner_restart_now(int restart_delay_s);

/* 1 once the bridge thread has been started. */
int wh_runner_is_started(void);
