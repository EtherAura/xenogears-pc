#ifndef PORT_SETTINGS_H
#define PORT_SETTINGS_H

/*
 * Settings (port/src/system/settings.c): the player's settings.ini, a small
 * key=value store any module can use.
 *
 *   settings.ini     in SDL's pref path: ~/.local/share/xenogears/ on Linux,
 *                    %APPDATA%\xenogears\ on Windows. One "key=value" per
 *                    line; lines starting with '#' or ';' are comments. Keys
 *                    are lower case. Comments, blank lines and keys no module
 *                    knows are kept when the file is written again.
 *   XG_<KEY>=value   overrides a key for one run (the key in upper case:
 *                    XG_ASPECT=1:1, XG_WINDOW_WIDTH=640). An override is never
 *                    written to the file; a change made during the run (a
 *                    hotkey) replaces it.
 *   XG_SETTINGS=file another settings file; XG_SETTINGS=none reads and writes
 *                    none (every key at its default unless overridden).
 *
 * The file is read on first use and written by settingsSave() (the display
 * hotkeys call it), and at exit when something changed since (the window's
 * size and position).
 */

/* The value of a key: the environment's override, else the file's, else
 * NULL. The text stays valid until the key is set again. */
const char* settingsGet(const char* key);
/* The environment's XG_<KEY> when it overrides the key now (it does until
 * the key is set during the run), else NULL: the settings menu shows it. */
const char* settingsOverride(const char* key);
/* An integer key (decimal or 0x hex), or def when unset or not a number. */
int settingsInt(const char* key, int def);
/* A yes/no key: 1/0, true/false, yes/no, on/off; def otherwise. */
int settingsBool(const char* key, int def);

/* Change a key for this run and for the file's next write. */
void settingsSet(const char* key, const char* value);
void settingsSetInt(const char* key, int value);
/* Remove a key from this run and the file (its default applies again; an
 * XG_<KEY> override no longer does). */
void settingsUnset(const char* key);

/* Hold a key at a value for this run (a netplay session: the settings that
 * change the game are the host's on both sides): it reads as that value
 * whatever the file or the environment say, settingsSet leaves it alone, and
 * it is never written. settingsPinned: the settings menu shows it locked. */
void settingsPin(const char* key, const char* value);
int settingsPinned(const char* key);

/* Changes with every settingsSet and settingsPin (not with the environment,
 * which is read as it is): a module that keeps a value it reads on a hot path
 * reads it again when this differs (speedup.c). */
unsigned settingsRevision(void);

/* One comment line written above a key when it is first added to the file. */
void settingsDescribe(const char* key, const char* comment);

/* Write the file now if a key changed since it was read or last written.
 * Returns 0 when it could not be written. */
int settingsSave(void);

/* The file's path, or NULL with XG_SETTINGS=none or no pref path. */
const char* settingsPath(void);

#endif /* PORT_SETTINGS_H */
