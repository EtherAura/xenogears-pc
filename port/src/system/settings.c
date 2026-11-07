/*
 * The player's settings.ini (settings.h): read on first use, kept in memory
 * line by line, so a rewrite changes only the values that changed and keeps
 * comments, blank lines and keys this build does not know.
 */

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

#define MAX_DOCS 128
#define MAX_DROPPED 128
#define MAX_PINS 64

typedef struct {
    char* key;   /* NULL: a comment, blank or unparsed line, kept as text */
    char* value;
    char* text;
} Line;

static struct {
    int loaded;
    int none; /* XG_SETTINGS=none, or no pref path */
    int existed;
    char path[1024];
    Line* lines;
    int count, cap;
    int dirty;
    char* dropped[MAX_DROPPED]; /* keys set this run: their XG_ overrides no longer apply */
    int droppedCount;
    struct {
        char* key;
        char* comment;
    } docs[MAX_DOCS];
    int docCount;
    struct {
        char* key;
        char* value;
    } pins[MAX_PINS]; /* settingsPin: held for this run */
    int pinCount;
    unsigned revision; /* settingsRevision */
} st;

static const char* pinned(const char* key) {
    for (int i = 0; i < st.pinCount; i++) {
        if (strcmp(st.pins[i].key, key) == 0) {
            return st.pins[i].value;
        }
    }
    return NULL;
}

static char* dup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

static Line* addLine(void) {
    if (st.count == st.cap) {
        st.cap = st.cap ? st.cap * 2 : 32;
        st.lines = realloc(st.lines, (size_t)st.cap * sizeof(*st.lines));
    }
    Line* l = &st.lines[st.count++];
    memset(l, 0, sizeof(*l));
    return l;
}

static void addText(const char* text) {
    addLine()->text = dup(text);
}

static void parseLine(char* raw) {
    char* s = trim(raw);
    char* eq = strchr(s, '=');
    if (*s == '\0' || *s == '#' || *s == ';' || *s == '[' || eq == NULL) {
        if (*s != '\0' && *s != '#' && *s != ';' && *s != '[') {
            fprintf(stderr, "settings: %s: ignoring '%s' (not key=value)\n", st.path, s);
        }
        addText(raw);
        return;
    }
    *eq = '\0';
    char* key = trim(s);
    for (char* p = key; *p != '\0'; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    Line* l = addLine();
    l->key = dup(key);
    l->value = dup(trim(eq + 1));
}

static void saveAtExit(void) {
    if (st.dirty) {
        settingsSave();
    }
}

/* SDL file APIs accept UTF-8 paths on Windows too. Keep fgets' line/chunk
 * behaviour so existing comments and unknown keys round-trip unchanged. */
static int readLine(SDL_IOStream* io, char* line, size_t size) {
    size_t n=0;
    char c;
    while (n+1<size && SDL_ReadIO(io,&c,1)==1) {
        line[n++]=c;
        if (c=='\n') break;
    }
    line[n]=0; return n!=0;
}

static void load(void) {
    if (st.loaded) {
        return;
    }
    st.loaded = 1;
    const char* env = SDL_getenv("XG_SETTINGS");
    if (env != NULL && env[0] != '\0') {
        if (strcmp(env, "none") == 0) {
            st.none = 1;
            return;
        }
        snprintf(st.path, sizeof(st.path), "%s", env);
    } else {
        char* pref = SDL_GetPrefPath(NULL, "xenogears");
        if (pref == NULL) {
            fprintf(stderr, "settings: no pref path (%s); settings are not kept\n", SDL_GetError());
            st.none = 1;
            return;
        }
        snprintf(st.path, sizeof(st.path), "%ssettings.ini", pref);
        SDL_free(pref);
    }
    SDL_IOStream* f = SDL_IOFromFile(st.path, "rb");
    if (f != NULL) {
        char line[4096]; /* User texture roots may be up to 3071 UTF-8 bytes. */
        st.existed = 1;
        while (readLine(f, line, sizeof(line))) {
            size_t n = strlen(line);
            if (n > 0 && line[n - 1] == '\n') {
                line[--n] = '\0';
            }
            if (n > 0 && line[n - 1] == '\r') {
                line[--n] = '\0';
            }
            parseLine(line);
        }
        SDL_CloseIO(f);
        fprintf(stderr, "settings: %s\n", st.path);
    } else {
        addText("# Xenogears PC port settings (port/src/system/settings.h): key=value.");
        addText("# XG_<KEY> in the environment overrides a key for one run.");
    }
    atexit(saveAtExit);
}

static Line* find(const char* key) {
    for (int i = st.count - 1; i >= 0; i--) {
        if (st.lines[i].key != NULL && strcmp(st.lines[i].key, key) == 0) {
            return &st.lines[i];
        }
    }
    return NULL;
}

static int isDropped(const char* key) {
    for (int i = 0; i < st.droppedCount; i++) {
        if (strcmp(st.dropped[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

/* XG_<KEY>: upper case, anything but letters and digits as '_'. */
static const char* override(const char* key) {
    char name[96] = "XG_";
    size_t n = 3;
    for (const char* p = key; *p != '\0' && n + 1 < sizeof(name); p++) {
        name[n++] = isalnum((unsigned char)*p) ? (char)toupper((unsigned char)*p) : '_';
    }
    name[n] = '\0';
    const char* v = SDL_getenv(name);
    return v != NULL && v[0] != '\0' ? v : NULL;
}

const char* settingsGet(const char* key) {
    load();
    const char* pin = pinned(key);
    if (pin != NULL) {
        return pin;
    }
    if (!isDropped(key)) {
        const char* v = override(key);
        if (v != NULL) {
            return v;
        }
    }
    Line* l = find(key);
    return l != NULL ? l->value : NULL;
}

const char* settingsOverride(const char* key) {
    load();
    return isDropped(key) || pinned(key) != NULL ? NULL : override(key);
}

unsigned settingsRevision(void) {
    return st.revision;
}

void settingsPin(const char* key, const char* value) {
    st.revision++;
    for (int i = 0; i < st.pinCount; i++) {
        if (strcmp(st.pins[i].key, key) == 0) {
            free(st.pins[i].value);
            st.pins[i].value = dup(value);
            return;
        }
    }
    if (st.pinCount < MAX_PINS) {
        st.pins[st.pinCount].key = dup(key);
        st.pins[st.pinCount].value = dup(value);
        st.pinCount++;
    }
}

int settingsPinned(const char* key) {
    return pinned(key) != NULL;
}

int settingsInt(const char* key, int def) {
    const char* v = settingsGet(key);
    if (v == NULL) {
        return def;
    }
    char* end;
    long n = strtol(v, &end, 0);
    return end != v && *end == '\0' ? (int)n : def;
}

int settingsBool(const char* key, int def) {
    static const char* const yes[] = {"1", "true", "yes", "on"};
    static const char* const no[] = {"0", "false", "no", "off"};
    const char* v = settingsGet(key);
    for (int i = 0; v != NULL && i < 4; i++) {
        if (SDL_strcasecmp(v, yes[i]) == 0) {
            return 1;
        }
        if (SDL_strcasecmp(v, no[i]) == 0) {
            return 0;
        }
    }
    return def;
}

void settingsSet(const char* key, const char* value) {
    load();
    st.revision++; /* even an equal value drops the key's XG_ override */
    if (pinned(key) != NULL) {
        return; /* held for this run (settingsPin): neither changed nor written */
    }
    if (!isDropped(key) && st.droppedCount < MAX_DROPPED) {
        st.dropped[st.droppedCount++] = dup(key);
    }
    Line* l = find(key);
    if (l != NULL) {
        if (strcmp(l->value, value) == 0) {
            return;
        }
        free(l->value);
        l->value = dup(value);
    } else {
        for (int i = 0; i < st.docCount; i++) {
            if (strcmp(st.docs[i].key, key) == 0) {
                char text[300];
                snprintf(text, sizeof(text), "# %s", st.docs[i].comment);
                addText(text);
            }
        }
        l = addLine();
        l->key = dup(key);
        l->value = dup(value);
    }
    st.dirty = 1;
}

void settingsUnset(const char* key) {
    load();
    st.revision++;
    if (pinned(key) != NULL) {
        return;
    }
    if (!isDropped(key) && st.droppedCount < MAX_DROPPED) {
        st.dropped[st.droppedCount++] = dup(key);
    }
    for (int i = 0; i < st.count; i++) {
        Line* l = &st.lines[i];
        if (l->key != NULL && strcmp(l->key, key) == 0) {
            free(l->key);
            free(l->value);
            free(l->text);
            memmove(l, l + 1, (size_t)(st.count - i - 1) * sizeof(*l));
            st.count--;
            st.dirty = 1;
            return;
        }
    }
}

void settingsSetInt(const char* key, int value) {
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    settingsSet(key, text);
}

void settingsDescribe(const char* key, const char* comment) {
    for (int i = 0; i < st.docCount; i++) {
        if (strcmp(st.docs[i].key, key) == 0) {
            return;
        }
    }
    if (st.docCount < MAX_DOCS) {
        st.docs[st.docCount].key = dup(key);
        st.docs[st.docCount].comment = dup(comment);
        st.docCount++;
    }
}

int settingsSave(void) {
    load();
    if (st.none || !st.dirty) {
        return !st.none;
    }
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", st.path);
    SDL_IOStream* f = SDL_IOFromFile(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "settings: cannot write %s\n", tmp);
        return 0;
    }
    int ok=1;
    for (int i = 0; ok && i < st.count; i++) {
        const Line* l = &st.lines[i];
        if (l->key != NULL) {
            size_t k=strlen(l->key), v=strlen(l->value);
            ok=SDL_WriteIO(f,l->key,k)==k && SDL_WriteIO(f,"=",1)==1 && SDL_WriteIO(f,l->value,v)==v;
        } else {
            size_t n=strlen(l->text);
            ok=SDL_WriteIO(f,l->text,n)==n;
        }
        if (ok) ok=SDL_WriteIO(f,"\n",1)==1;
    }
    if (!SDL_CloseIO(f)) ok=0;
    /* Replace the file in one step, so a crash never leaves half of it. */
    if (!ok || !SDL_RenamePath(tmp, st.path)) {
        fprintf(stderr, "settings: cannot replace %s\n", st.path);
        SDL_RemovePath(tmp);
        return 0;
    }
    if (!st.existed) {
        st.existed = 1;
        fprintf(stderr, "settings: wrote %s\n", st.path);
    }
    st.dirty = 0;
    return 1;
}

const char* settingsPath(void) {
    load();
    return st.none ? NULL : st.path;
}
