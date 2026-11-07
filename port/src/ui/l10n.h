/*
 * The interface's languages: the settings menu's and the extras' own text
 * (l10n.cpp). Not the game's text: that is the disc's.
 *
 * A string is looked up by its English text as the sources have it (the
 * gettext convention): tr("Display") gives the chosen language's text, or
 * "Display" itself when the language has none, so nothing is ever blank and
 * English needs no table. An ImGui id suffix ("Back##help", "###id") is not
 * part of the lookup: the translation keeps the source's suffix, so ids stay
 * unique. printf formats are looked up before formatting:
 * hint(tr("Found %d images"), n); format_arg keeps the compiler's checks on
 * the English literal, and a translation whose conversions differ from it is
 * never used (it could crash the formatting).
 *
 * Catalogs are gettext PO files: built in, port/src/ui/lang/<code>.po
 * (embedded by tools/l10n.py embed at build time); the player's own,
 * <save folder>/lang/<code>.po (XG_LANG_DIR overrides the folder, "none"
 * reads none), read when the language list is first needed; one with a
 * built-in code adds to and overrides that catalog's entries. The header's
 * "Language:" is the code, "X-Language-Name:" the name shown (in that
 * language). tools/l10n.py extracts the English strings and checks catalogs.
 *
 *   ui_language        (settings.ini; F1 > System > Language) en, ja, or a
 *                      player's catalog's code; unset, the build's language
 *                      (US English, JP Japanese); XG_UI_LANGUAGE overrides it
 *                      for a run. Applies at once.
 *   XG_L10N_MISSING=file  every English string looked up that the chosen
 *                      language lacks, once each (a coverage run).
 */

#ifndef PORT_UI_L10N_H
#define PORT_UI_L10N_H

#if defined(__GNUC__) || defined(__clang__)
#define L10N_FORMAT_ARG __attribute__((format_arg(1)))
#else
#define L10N_FORMAT_ARG
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The chosen language's text for an English string (itself if none). The
 * result stays valid for the rest of the run. */
const char* l10nText(const char* english) L10N_FORMAT_ARG;

/* Marks a string for extraction where it is not translated at once (a
 * table of names translated where they are shown). */
#define N_(s) s

/* The languages: 0 is English; then the built-in ones, then the player's. */
int l10nLanguageCount(void);
const char* l10nLanguageCode(int i);  /* "en", "ja", ... */
const char* l10nLanguageName(int i);  /* "English", "日本語", ... */
int l10nLanguage(void);               /* the chosen one */
void l10nSetLanguage(int i);          /* and settings.ini's ui_language */
int l10nSerial(void);                 /* changes with the language (for caches of translated text) */
int l10nEntries(int i);               /* a language's usable entries (0 for English) */
void l10nReload(void);                /* read the player's catalogs again (the chosen language kept) */

#ifdef __cplusplus
}

inline const char* tr(const char* english) L10N_FORMAT_ARG;
inline const char* tr(const char* english) {
    return l10nText(english);
}
#endif

#endif /* PORT_UI_L10N_H */
