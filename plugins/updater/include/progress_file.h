#pragma once

/* Publishes the state of a long operation for the ShellUI, which shows the
 * native progress panel (same as when a plugin is started or stopped). */
void progress_begin(const char *title, const char *phase);
void progress_update(unsigned percent, const char *phase);
void progress_end(int ok, const char *phase);
