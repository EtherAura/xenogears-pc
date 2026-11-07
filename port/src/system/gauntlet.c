/*
 * Gauntlets: the run's plan and state, its settings and its records
 * (gauntlet.h). The game's side (the battles, the checkpoint, the recovery)
 * is port/src/game/main/gauntlet.c; the menu is port/src/ui/gauntlet.cpp.
 */
#include "system/gauntlet.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "system/hw.h"
#include "system/savestate.h"
#include "system/settings.h"

/* The extras' boss list (absent from a build without them). */
XgGauntletScan* xgGauntletScanBegin(void) __attribute__((weak));
int xgGauntletScanStep(XgGauntletScan* s, int budget) __attribute__((weak));
int xgGauntletScanResult(XgGauntletScan* s, PcGauntletBoss** out) __attribute__((weak));
void xgGauntletScanFree(XgGauntletScan* s) __attribute__((weak));

typedef struct {
    unsigned char table[PC_GAUNTLET_TABLE];
    int config, gear, music;
    unsigned char party[3];
    char key[16];
    char name[160];
} Fight;

static struct {
    int state;
    int count, current, retries, skipped, quitting, recovery;
    uint32_t started;
    char name[64];
    Fight fights[PC_GAUNTLET_MAX];
    int lastResult;
    int autostarted;
} R = {.lastResult = -1};

static const char* const presetNames[PC_GAUNTLET_PRESETS] = {"story", "character", "gear", "custom"};
static const char* const presetLabels[PC_GAUNTLET_PRESETS] = {"Every boss, story order", "Character bosses",
                                                             "Gear bosses", "Custom"};
static const char* const recoveryNames[PC_GAUNTLET_RECOVERIES] = {"full", "none", "25", "50", "revive"};
static const char* const recoveryLabels[PC_GAUNTLET_RECOVERIES] = {"Full", "None", "25%", "50%", "Revive only"};

void pcGauntletKey(const PcGauntletBoss* b, char* out, int size) {
    snprintf(out, (size_t)size, "%d-%03d-%d", b->disc, b->field, b->config);
}

int pcGauntletPresetOf(const char* name) {
    for (int i = 0; name != NULL && i < PC_GAUNTLET_PRESETS; i++) {
        if (strcmp(name, presetNames[i]) == 0) return i;
    }
    return -1;
}

const char* pcGauntletPresetName(int preset) {
    return preset >= 0 && preset < PC_GAUNTLET_PRESETS ? presetNames[preset] : "";
}

const char* pcGauntletPresetLabel(int preset) {
    return preset >= 0 && preset < PC_GAUNTLET_PRESETS ? presetLabels[preset] : "";
}

const char* pcGauntletRecoveryName(int recovery) {
    return recovery >= 0 && recovery < PC_GAUNTLET_RECOVERIES ? recoveryNames[recovery] : "";
}

const char* pcGauntletRecoveryLabel(int recovery) {
    return recovery >= 0 && recovery < PC_GAUNTLET_RECOVERIES ? recoveryLabels[recovery] : "";
}

int pcGauntletPreset(void) {
    int p = pcGauntletPresetOf(settingsGet("gauntlet_preset"));
    return p < 0 ? PC_GAUNTLET_STORY : p;
}

void pcGauntletSetPreset(int preset) {
    settingsDescribe("gauntlet_preset", "Gauntlet: story (every boss), character, gear or custom");
    settingsSet("gauntlet_preset", pcGauntletPresetName(preset < 0 || preset >= PC_GAUNTLET_PRESETS ? 0 : preset));
}

int pcGauntletRecoverySetting(void) {
    const char* v = settingsGet("gauntlet_recovery");
    for (int i = 0; v != NULL && i < PC_GAUNTLET_RECOVERIES; i++) {
        if (strcmp(v, recoveryNames[i]) == 0) return i;
    }
    return PC_GAUNTLET_FULL;
}

void pcGauntletSetRecovery(int recovery) {
    settingsDescribe("gauntlet_recovery", "Gauntlet recovery between fights: full, none, 25, 50 or revive");
    settingsSet("gauntlet_recovery",
                pcGauntletRecoveryName(recovery < 0 || recovery >= PC_GAUNTLET_RECOVERIES ? 0 : recovery));
}

/* The boss whose key is the text at s (up to a separator); -1 none. */
static int bossOfKey(const PcGauntletBoss* bosses, int count, const char* s) {
    int disc, field, config;
    if (sscanf(s, "%d-%d-%d", &disc, &field, &config) != 3) return -1;
    for (int i = 0; i < count; i++) {
        if (bosses[i].disc == disc && bosses[i].field == field && bosses[i].config == config) return i;
    }
    return -1;
}

int pcGauntletSelect(int preset, const PcGauntletBoss* bosses, int count, int* out, int max) {
    int n = 0;
    if (preset == PC_GAUNTLET_CUSTOM) {
        const char* list = settingsGet("gauntlet_custom");
        while (list != NULL && *list && n < max) {
            int b = bossOfKey(bosses, count, list);
            if (b >= 0) out[n++] = b;
            const char* comma = strchr(list, ',');
            list = comma != NULL ? comma + 1 : NULL;
        }
        return n;
    }
    for (int i = 0; i < count && n < max; i++) {
        if (preset == PC_GAUNTLET_CHARACTER && bosses[i].gear) continue;
        if (preset == PC_GAUNTLET_GEAR && !bosses[i].gear) continue;
        out[n++] = i;
    }
    return n;
}

void pcGauntletSetCustom(const PcGauntletBoss* bosses, const int* list, int count) {
    size_t cap = (size_t)count * 16 + 1, len = 0;
    char* s = calloc(cap, 1);
    if (s == NULL) return;
    for (int i = 0; i < count; i++) {
        char key[16];
        pcGauntletKey(&bosses[list[i]], key, sizeof key);
        len += (size_t)snprintf(s + len, cap - len, "%s%s", i ? "," : "", key);
    }
    settingsDescribe("gauntlet_custom", "Gauntlet: the custom list of bosses (disc-field-config, in order)");
    settingsSet("gauntlet_custom", s);
    free(s);
}

/* The entry for key in gauntlet_parties: its text after '=', or NULL. */
static const char* partyEntry(const char* all, const char* key) {
    size_t k = strlen(key);
    for (const char* p = all; p != NULL && *p;) {
        if (strncmp(p, key, k) == 0 && p[k] == '=') return p + k + 1;
        p = strchr(p, ';');
        if (p != NULL) p++;
    }
    return NULL;
}

int pcGauntletPartyFor(const PcGauntletBoss* b, unsigned char party[3]) {
    char key[16];
    pcGauntletKey(b, key, sizeof key);
    const char* e = partyEntry(settingsGet("gauntlet_parties"), key);
    party[0] = party[1] = party[2] = 0xFF;
    if (e == NULL) return 0;
    for (int i = 0; i < 3 && *e && *e != ';'; i++) {
        if (*e >= '0' && *e <= '9') party[i] = (unsigned char)strtol(e, (char**)&e, 10);
        else e++; /* x: none */
        if (*e == '.') e++;
    }
    return 1;
}

void pcGauntletSetPartyFor(const PcGauntletBoss* b, const unsigned char party[3]) {
    char key[16], entry[40];
    pcGauntletKey(b, key, sizeof key);
    int n = snprintf(entry, sizeof entry, "%s=", key);
    for (int i = 0; i < 3; i++) {
        if (party[i] == 0xFF) n += snprintf(entry + n, sizeof entry - (size_t)n, "%sx", i ? "." : "");
        else n += snprintf(entry + n, sizeof entry - (size_t)n, "%s%d", i ? "." : "", party[i]);
    }
    const char* all = settingsGet("gauntlet_parties");
    size_t cap = (all ? strlen(all) : 0) + sizeof entry + 2, len = 0;
    char* s = calloc(cap, 1);
    if (s == NULL) return;
    size_t k = strlen(key);
    for (const char* p = all; p != NULL && *p;) { /* the others, then this one */
        const char* end = strchr(p, ';');
        size_t l = end ? (size_t)(end - p) : strlen(p);
        if (!(l > k && strncmp(p, key, k) == 0 && p[k] == '=') && l > 0) {
            len += (size_t)snprintf(s + len, cap - len, "%.*s;", (int)l, p);
        }
        p = end ? end + 1 : NULL;
    }
    snprintf(s + len, cap - len, "%s", entry);
    settingsDescribe("gauntlet_parties", "Gauntlet: the party picked for each boss (disc-field-config=a.b.c)");
    settingsSet("gauntlet_parties", s);
    free(s);
}

int pcGauntletFightParty(const PcGauntletBoss* b, const PcGauntletRoster* roster, unsigned char out[3]) {
    unsigned char want[3];
    int n = 0;
    if (!pcGauntletPartyFor(b, want)) memcpy(want, roster->party, 3);
    out[0] = out[1] = out[2] = 0xFF;
    for (int i = 0; i < 3; i++) {
        int at = -1;
        for (int k = 0; k < roster->count; k++) {
            if (roster->ids[k] == want[i]) at = k;
        }
        if (at < 0 || (b->gear && !roster->hasGear[at])) continue;
        int twice = 0;
        for (int k = 0; k < n; k++) twice |= out[k] == want[i];
        if (!twice) out[n++] = want[i];
    }
    return n;
}

/* system/netplay.c (weak: tests link this file without it). */
int netplayRefuses(const char* what) __attribute__((weak)); /* system/netplay.c: not in an online session */

int pcGauntletBegin(const char* name, const PcGauntletBoss* bosses, const int* picks, int count,
                    const PcGauntletRoster* roster) {
    if (netplayRefuses != NULL && netplayRefuses("a gauntlet")) return 0; /* one side alone: apart */
    if (R.state != PC_GAUNTLET_IDLE || roster->count == 0) return 0;
    R.count = R.skipped = 0;
    for (int i = 0; i < count && R.count < PC_GAUNTLET_MAX; i++) {
        const PcGauntletBoss* b = &bosses[picks[i]];
        Fight* f = &R.fights[R.count];
        if (pcGauntletFightParty(b, roster, f->party) == 0) {
            R.skipped++;
            continue;
        }
        memcpy(f->table, b->table, sizeof f->table);
        f->config = b->config;
        f->gear = b->gear;
        f->music = b->music;
        pcGauntletKey(b, f->key, sizeof f->key);
        snprintf(f->name, sizeof f->name, "%s", b->name);
        R.count++;
    }
    if (R.count == 0) return 0;
    snprintf(R.name, sizeof R.name, "%s", name);
    R.current = R.retries = R.quitting = 0;
    R.recovery = pcGauntletRecoverySetting();
    R.state = PC_GAUNTLET_WAITING;
    fprintf(stderr, "gauntlet: %s, %d fights (%d skipped), recovery %s; waiting for the field or the world map\n",
            R.name, R.count, R.skipped, recoveryNames[R.recovery]);
    return R.count;
}

void pcGauntletCancel(void) {
    if (R.state == PC_GAUNTLET_WAITING) {
        R.state = PC_GAUNTLET_IDLE;
    } else if (R.state == PC_GAUNTLET_RUNNING) {
        R.quitting = 1;
    }
}

int pcGauntletStatus(int* current, int* count, int* retries, int* skipped, int* quitting) {
    if (current) *current = R.current;
    if (count) *count = R.count;
    if (retries) *retries = R.retries;
    if (skipped) *skipped = R.skipped;
    if (quitting) *quitting = R.quitting;
    return R.state;
}

const char* pcGauntletName(void) {
    return R.name;
}

const char* pcGauntletFightName(int index) {
    return index >= 0 && index < R.count ? R.fights[index].name : "";
}

int pcGauntletLastResult(void) {
    return R.lastResult;
}

/* ---- the game's side ---- */

int pcGauntletState(void) {
    if (R.state == PC_GAUNTLET_IDLE && !R.autostarted) {
        pcGauntletPoll(); /* XG_GAUNTLET_AUTOSTART */
    }
    return R.state;
}

int pcGauntletTakeStart(void) {
    if (R.state != PC_GAUNTLET_WAITING) return 0;
    R.state = PC_GAUNTLET_RUNNING;
    R.started = hwVSyncCount();
    fprintf(stderr, "gauntlet: fight 1 of %d: %s\n", R.count, R.fights[0].name);
    return 1;
}

int pcGauntletFight(int index, unsigned char* table, unsigned char* party, int* gear) {
    if (R.state == PC_GAUNTLET_IDLE || index < 0 || index >= R.count) return -1;
    if (table) memcpy(table, R.fights[index].table, PC_GAUNTLET_TABLE);
    if (party) memcpy(party, R.fights[index].party, 3);
    if (gear) *gear = R.fights[index].gear;
    return R.fights[index].config;
}

int pcGauntletFightMusic(int index) {
    return R.state != PC_GAUNTLET_IDLE && index >= 0 && index < R.count ? R.fights[index].music : -1;
}

int pcGauntletCurrent(void) {
    return R.current;
}

void pcGauntletNext(void) {
    R.current++;
    if (R.current < R.count) {
        fprintf(stderr, "gauntlet: fight %d of %d: %s\n", R.current + 1, R.count, R.fights[R.current].name);
    }
}

void pcGauntletRetried(void) {
    R.retries++;
    fprintf(stderr, "gauntlet: retry %d (fight %d)\n", R.retries, R.current + 1);
}

int pcGauntletRecovery(void) {
    return R.recovery;
}

int pcGauntletQuitting(void) {
    return R.quitting;
}

/* ---- records ---- */

const char* pcGauntletRecordsPath(void) {
    static char path[4096];
    const char* env = getenv("XG_GAUNTLET_RECORDS");
    if (env != NULL && *env) return env;
    const char* settings = settingsPath();
    if (settings == NULL) return NULL;
    snprintf(path, sizeof path, "%s", settings);
    char* slash = strrchr(path, '/');
#ifdef _WIN32
    char* back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
#endif
    if (slash == NULL) return "gauntlets.json";
    snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "gauntlets.json");
    return path;
}

static char* readAll(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (s != NULL) {
        size_t got = fread(s, 1, (size_t)n, f);
        s[got] = '\0';
    }
    fclose(f);
    return s;
}

static const char* difficulty(void) {
    if (pcGameplayHardMode()) return "hard";
    for (int i = 0; i < GAMEPLAY_ENEMY_STAT_COUNT; i++) {
        if (pcGameplayEnemyMultiplier(i) != 100) return "custom";
    }
    return "normal";
}

static void writeRecord(uint32_t vblanks) {
    const char* path = pcGauntletRecordsPath();
    if (path == NULL) return;
    char when[24];
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    if (tm == NULL || strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm) == 0) snprintf(when, sizeof when, "?");
    size_t cap = 512 + (size_t)R.count * 32, len = 0;
    char* rec = malloc(cap);
    if (rec == NULL) return;
    len += (size_t)snprintf(rec + len, cap - len,
                            "{\"gauntlet\":\"%s\",\"fights\":%d,\"skipped\":%d,\"retries\":%d,\"vblanks\":%u,"
                            "\"seconds\":%.1f,\"recovery\":\"%s\",\"difficulty\":\"%s\",\"when\":\"%s\",\"bosses\":[",
                            R.name, R.count, R.skipped, R.retries, vblanks, vblanks / 59.94, recoveryNames[R.recovery],
                            difficulty(), when);
    for (int i = 0; i < R.count; i++) {
        len += (size_t)snprintf(rec + len, cap - len, "%s\"%s\"", i ? "," : "", R.fights[i].key);
    }
    len += (size_t)snprintf(rec + len, cap - len, "],\"parties\":[");
    for (int i = 0; i < R.count; i++) {
        const unsigned char* p = R.fights[i].party;
        len += (size_t)snprintf(rec + len, cap - len, "%s[", i ? "," : "");
        for (int k = 0; k < 3 && p[k] != 0xFF; k++) len += (size_t)snprintf(rec + len, cap - len, "%s%d", k ? "," : "", p[k]);
        len += (size_t)snprintf(rec + len, cap - len, "]");
    }
    snprintf(rec + len, cap - len, "]}");
    char* old = readAll(path);
    char* end = old != NULL ? strrchr(old, ']') : NULL;
    char temp[4200];
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE* f = fopen(temp, "wb");
    if (f != NULL) {
        int first = old == NULL || end == NULL || strchr(old, '{') == NULL;
        if (first) {
            fprintf(f, "[\n%s\n]\n", rec);
        } else {
            while (end > old && (end[-1] == '\n' || end[-1] == ' ')) end--;
            fprintf(f, "%.*s,\n%s\n]\n", (int)(end - old), old, rec);
        }
        int ok = fclose(f) == 0;
        if (ok) {
            remove(path); /* Windows' rename does not replace */
            ok = rename(temp, path) == 0;
        }
        fprintf(stderr, "gauntlet: record %s %s\n", ok ? "written to" : "not written to", path);
    }
    free(old);
    free(rec);
}

static void field(const char* rec, const char* key, char* out, size_t size) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char* p = strstr(rec, pat);
    out[0] = '\0';
    if (p == NULL) return;
    p += strlen(pat);
    if (*p == '"') {
        p++;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < size) out[n++] = *p++;
        out[n] = '\0';
    } else {
        size_t n = 0;
        while (*p && (*p == '-' || (*p >= '0' && *p <= '9')) && n + 1 < size) out[n++] = *p++;
        out[n] = '\0';
    }
}

int pcGauntletRecords(PcGauntletRecord* out, int max) {
    const char* path = pcGauntletRecordsPath();
    char* s = path ? readAll(path) : NULL;
    int n = 0;
    for (char* line = s; line != NULL && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        if (strchr(line, '{') != NULL) {
            PcGauntletRecord r;
            char num[24];
            memset(&r, 0, sizeof r);
            field(line, "gauntlet", r.gauntlet, sizeof r.gauntlet);
            field(line, "fights", num, sizeof num), r.fights = atoi(num);
            field(line, "skipped", num, sizeof num), r.skipped = atoi(num);
            field(line, "retries", num, sizeof num), r.retries = atoi(num);
            field(line, "vblanks", num, sizeof num), r.vblanks = (unsigned)strtoul(num, NULL, 10);
            field(line, "recovery", r.recovery, sizeof r.recovery);
            field(line, "difficulty", r.difficulty, sizeof r.difficulty);
            field(line, "when", r.when, sizeof r.when);
            if (n < max) {
                memmove(out + 1, out, sizeof *out * (size_t)n); /* the newest first */
                out[0] = r;
                n++;
            } else if (max > 0) {
                memmove(out + 1, out, sizeof *out * (size_t)(max - 1));
                out[0] = r;
            }
        }
        line = next;
    }
    free(s);
    return n;
}

void pcGauntletFinish(int completed) {
    if (R.state == PC_GAUNTLET_IDLE) return;
    uint32_t vblanks = hwVSyncCount() - R.started;
    fprintf(stderr, "gauntlet: %s after %d of %d fights, %d retries, %u vblanks\n", completed ? "cleared" : "ended",
            completed ? R.count : R.current, R.count, R.retries, vblanks);
    if (completed) writeRecord(vblanks);
    R.lastResult = completed != 0;
    R.state = PC_GAUNTLET_IDLE;
}

/* ---- the menu's roster, the env's autostart ---- */

void pcGauntletRoster(PcGauntletRoster* out) {
    memset(out, 0, sizeof *out);
    out->party[0] = out->party[1] = out->party[2] = 0xFF;
    out->count = pcGauntletGameRoster(out->ids, out->hasGear, &out->rawNames[0][0], out->party);
}

void pcGauntletPoll(void) {
    static int checked;
    static uint32_t when;
    if (!checked) {
        const char* at = getenv("XG_GAUNTLET_AUTOSTART");
        checked = 1;
        R.autostarted = at == NULL || *at == '\0';
        when = R.autostarted ? 0 : (uint32_t)strtoul(at, NULL, 0);
    }
    if (R.autostarted || R.state != PC_GAUNTLET_IDLE || hwVSyncCount() < when) {
        return;
    }
    R.autostarted = 1;
    if (xgGauntletScanBegin == NULL) {
        fprintf(stderr, "gauntlet: autostart needs the extras (the boss list)\n");
        return;
    }
    XgGauntletScan* scan = xgGauntletScanBegin();
    PcGauntletBoss* bosses = NULL;
    int count = 0;
    if (scan != NULL) {
        while (!xgGauntletScanStep(scan, 64)) {
        }
        count = xgGauntletScanResult(scan, &bosses);
        xgGauntletScanFree(scan);
    }
    static int picks[PC_GAUNTLET_MAX];
    int preset = pcGauntletPreset();
    int n = pcGauntletSelect(preset, bosses, count, picks, PC_GAUNTLET_MAX);
    int max = (int)strtol(getenv("XG_GAUNTLET_MAX") ? getenv("XG_GAUNTLET_MAX") : "0", NULL, 0);
    if (max > 0 && n > max) n = max;
    PcGauntletRoster roster;
    pcGauntletRoster(&roster);
    fprintf(stderr, "gauntlet: autostart: %d bosses, preset %s: %d picked\n", count, presetNames[preset], n);
    for (int i = 0; i < n; i++) {
        char key[16];
        pcGauntletKey(&bosses[picks[i]], key, sizeof key);
        const unsigned char* cfg = bosses[picks[i]].table + 0x20 * bosses[picks[i]].config;
        fprintf(stderr, "gauntlet:   %s %s story %d%s music %d (set %d flags %02x event %d): %s\n", key,
                bosses[picks[i]].gear ? "gear" : "char", bosses[picks[i]].story, bosses[picks[i]].dated ? "" : "?",
                bosses[picks[i]].music, cfg[0], cfg[1], cfg[3], bosses[picks[i]].name);
    }
    pcGauntletBegin(presetNames[preset], bosses, picks, n, &roster);
    free(bosses);
}

/* ---- save states (savestate.h) ---- */

/* The run in progress (no pointers); whether XG_GAUNTLET_AUTOSTART already
 * started one stays this process's. The records file is never rolled back. */
void gauntletStateIo(XgState* s) {
    int autostarted = R.autostarted;
    R.autostarted = 0; /* not in the state */
    XG_STATE_VAR(s, R);
    R.autostarted = autostarted;
}
