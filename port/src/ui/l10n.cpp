// The interface's languages (l10n.h): built-in PO catalogs embedded at build
// time (gen/l10n_catalogs.h, tools/l10n.py embed) and the player's own from
// <save folder>/lang/*.po, looked up by the English text.

#include "l10n.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "system/settings.h"
}

namespace skin {
bool native(); // skin.cpp: the game's font is the menu's (else ImGui's own: Latin only)
}

namespace {

// The interface's language until the player chooses one: the release's own
// (the user's choice, xg-htn.35.22 Q2).
#ifdef VERSION_JP
const char* const BUILD_LANGUAGE = "ja";
#else
const char* const BUILD_LANGUAGE = "en";
#endif

struct L10nPair {
    const char* english;
    const char* text;
};
struct L10nBuiltin {
    const char* code;
    const char* name;
    const L10nPair* pairs;
    int count;
};
#include "l10n_catalogs.h"

struct Language {
    std::string code, name;
    std::unordered_map<std::string_view, const char*> text; // English -> translation
    bool gameFont = false; // a character past Latin (kana, kanji...): needs the game's font and its fallback
};

// Characters past Latin Extended: only the game's font (with the pixel
// fallback, assets.cpp) draws them; ImGui's own font, the menu's look
// without a disc, shows them as boxes.
bool pastLatin(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (*p >= 0xC9 && *p < 0xF8) return true; // a UTF-8 lead of U+0250 and above
    }
    return false;
}

struct State {
    std::once_flag once;
    std::mutex lock;                  // the composite labels and the missing log
    std::vector<Language> languages;  // [0] English
    std::deque<std::string> store;    // the player's catalogs' strings (stable addresses)
    int chosen = 0;
    int serial = 1;
    std::unordered_map<std::string, std::string> labels; // "<lang>\x1f<source>" -> translation + id suffix
    std::unordered_set<std::string> missing;
    FILE* missingLog = nullptr;
} st;

// ---- checks shared with tools/l10n.py ------------------------------------------------------------

// The printf conversions of a string, in order (%% left out), as what they
// read: a '*' for each width or precision argument, then the length and the
// type. A translation must read the same, or formatting it could read the
// wrong arguments; flags, widths and precisions may differ ("%.0s" prints
// nothing of its string: a plural's "s").
std::vector<std::string> conversions(const char* s) {
    std::vector<std::string> out;
    for (const char* p = s; *p; p++) {
        if (*p != '%') continue;
        const char* q = p + 1;
        if (*q == '%') {
            p = q;
            continue;
        }
        std::string read;
        while (*q && strchr("-+#0", *q)) q++; // not the space flag: "11% taller" is no conversion
        if (*q == '*') q++, read += '*';
        else while (*q >= '0' && *q <= '9') q++;
        if (*q == '.') {
            q++;
            if (*q == '*') q++, read += '*';
            else while (*q >= '0' && *q <= '9') q++;
        }
        const char* length = q;
        while (*q && strchr("hlLzjt", *q)) q++;
        if (*q && strchr("diouxXeEfFgGaAcspn", *q)) {
            read.append(length, (size_t)(q + 1 - length));
            out.push_back(read);
            p = q;
        } else {
            out.push_back("?" + std::string(p, (size_t)(q - p))); // malformed: never equal by accident
            if (!*q) break;
            p = q;
        }
    }
    return out;
}

// Why a translation cannot be used, or nullptr.
const char* refuse(const char* english, const char* text) {
    if (text == nullptr || text[0] == '\0') return "empty";
    if (conversions(english) != conversions(text)) return "its printf conversions differ from the English";
    if (strstr(text, "##") != nullptr) return "an ImGui id (##) in the translation";
    return nullptr;
}

// ---- the player's PO files -------------------------------------------------------------------------

// A PO string's quoted text, unescaped; false when the line is not one.
bool unquote(const char* p, std::string& out) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"') return false;
    for (; *p && *p != '"'; p++) {
        if (*p != '\\') {
            out += *p;
            continue;
        }
        switch (*++p) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '\0': return false;
        default: out += '\\'; out += *p; break;
        }
    }
    return *p == '"';
}

// Read one PO file into `lang` (entries added; a later one replaces an
// earlier one). Returns the entries used, -1 when the file is unreadable.
int readPo(const std::string& path, Language& lang, std::string* code, std::string* name) {
    SDL_IOStream* f = SDL_IOFromFile(path.c_str(), "rb");
    if (f == nullptr) return -1;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = SDL_ReadIO(f, buf, sizeof buf)) > 0) data.append(buf, n);
    SDL_CloseIO(f);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) data.erase(0, 3);

    int used = 0, refused = 0, lineNo = 0;
    std::string msgid, msgstr, *field = nullptr;
    bool fuzzy = false, have = false, obsolete = false;
    auto flush = [&]() {
        if (have && !obsolete) {
            if (msgid.empty()) {
                // the header: "Language: ja\nX-Language-Name: 日本語\n"
                size_t at = 0;
                while (at < msgstr.size()) {
                    size_t end = msgstr.find('\n', at);
                    std::string line = msgstr.substr(at, end == std::string::npos ? std::string::npos : end - at);
                    at = end == std::string::npos ? msgstr.size() : end + 1;
                    size_t colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    std::string key = line.substr(0, colon), value = line.substr(colon + 1);
                    value.erase(0, value.find_first_not_of(' '));
                    value.erase(value.find_last_not_of(" \r") + 1);
                    if (key == "Language" && code) *code = value;
                    if (key == "X-Language-Name" && name) *name = value;
                }
            } else if (!fuzzy) {
                const char* why = refuse(msgid.c_str(), msgstr.c_str());
                if (why == nullptr) {
                    st.store.push_back(msgid);
                    std::string_view key = st.store.back();
                    st.store.push_back(msgstr);
                    lang.text[key] = st.store.back().c_str();
                    lang.gameFont = lang.gameFont || pastLatin(msgstr.c_str());
                    used++;
                } else if (msgstr[0] != '\0') {
                    refused++;
                    fprintf(stderr, "l10n: %s: \"%.40s\" not used: %s\n", path.c_str(), msgid.c_str(), why);
                }
            }
        }
        msgid.clear();
        msgstr.clear();
        field = nullptr;
        fuzzy = have = obsolete = false;
    };
    size_t at = 0;
    while (at <= data.size()) {
        size_t end = data.find('\n', at);
        std::string line = data.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? data.size() + 1 : end + 1;
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        bool old = line.compare(0, 2, "#~") == 0; // an obsolete entry: read, not used
        if (old) line.erase(0, 2);
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            flush();
            continue;
        }
        if (line[0] == '#' && !old) {
            if (have && field == &msgstr) flush();
            if (line.compare(0, 2, "#,") == 0 && line.find("fuzzy") != std::string::npos) fuzzy = true;
            continue;
        }
        const char* p = line.c_str() + first;
        std::string* next = nullptr;
        if (!strncmp(p, "msgctxt", 7) && (p[7] == ' ' || p[7] == '\t')) {
            next = nullptr; // contexts are not used by the port: the entry is read for its msgid
            if (have && field == &msgstr) flush();
            have = true;
            obsolete = obsolete || old;
            std::string ignored;
            if (!unquote(p + 7, ignored)) goto bad;
            field = nullptr;
            continue;
        } else if (!strncmp(p, "msgid", 5) && (p[5] == ' ' || p[5] == '\t')) {
            if (have && field == &msgstr) flush();
            next = &msgid;
            p += 5;
        } else if (!strncmp(p, "msgstr", 6) && (p[6] == ' ' || p[6] == '\t')) {
            next = &msgstr;
            p += 6;
        } else if (*p == '"' && field != nullptr) {
            if (!unquote(p, *field)) goto bad;
            continue;
        } else if (*p == '"') {
            continue; // a msgctxt's continuation
        } else {
            goto bad;
        }
        have = true;
        obsolete = obsolete || old;
        field = next;
        field->clear();
        if (!unquote(p, *field)) goto bad;
        continue;
    bad:
        fprintf(stderr, "l10n: %s:%d: cannot read this line; the rest of the file is not used\n", path.c_str(), lineNo);
        break;
    }
    flush();
    if (refused) fprintf(stderr, "l10n: %s: %d entries not used\n", path.c_str(), refused);
    return used;
}

// The player's catalogs: <save folder>/lang/*.po (XG_LANG_DIR overrides).
void readPlayerCatalogs() {
    const char* env = SDL_getenv("XG_LANG_DIR");
    std::string dir;
    if (env != nullptr && env[0] != '\0') {
        if (!strcmp(env, "none")) return;
        dir = env;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    } else {
        char* pref = SDL_GetPrefPath(NULL, "xenogears");
        if (pref == nullptr) return;
        dir = std::string(pref) + "lang/";
        SDL_free(pref);
    }
    int count = 0;
    char** names = SDL_GlobDirectory(dir.c_str(), "*.po", SDL_GLOB_CASEINSENSITIVE, &count);
    if (names == nullptr) return;
    std::vector<std::string> files(names, names + count);
    SDL_free(names);
    std::sort(files.begin(), files.end());
    for (const std::string& file : files) {
        Language lang;
        std::string code, name;
        int used = readPo(dir + file, lang, &code, &name);
        if (used < 0) continue;
        if (code.empty()) code = file.substr(0, file.size() - 3);
        if (name.empty()) name = code;
        auto same = std::find_if(st.languages.begin(), st.languages.end(),
                                 [&](const Language& l) { return SDL_strcasecmp(l.code.c_str(), code.c_str()) == 0; });
        if (same == st.languages.begin()) {
            fprintf(stderr, "l10n: %s%s: English is the sources' own text; not used\n", dir.c_str(), file.c_str());
            continue;
        }
        if (same != st.languages.end()) {
            for (auto& e : lang.text) same->text[e.first] = e.second;
            same->gameFont = same->gameFont || lang.gameFont;
            fprintf(stderr, "l10n: %s%s: %d entries over the built-in %s\n", dir.c_str(), file.c_str(), used,
                    code.c_str());
        } else {
            lang.code = code;
            lang.name = name;
            st.languages.push_back(std::move(lang));
            fprintf(stderr, "l10n: %s%s: %s (%s), %d entries\n", dir.c_str(), file.c_str(), name.c_str(),
                    code.c_str(), used);
        }
    }
}

int find(const char* code) {
    for (size_t i = 0; code != nullptr && i < st.languages.size(); i++) {
        if (SDL_strcasecmp(st.languages[i].code.c_str(), code) == 0) return (int)i;
    }
    return -1;
}

// The languages: English, the built-in catalogs, the player's. A reload keeps
// the strings read before (a pointer handed out stays valid for the run).
void load(const char* keep) {
    st.languages.clear();
    Language en;
    en.code = "en";
    en.name = "English";
    st.languages.push_back(std::move(en));
    for (const L10nBuiltin& b : l10nBuiltins) {
        if (b.code == nullptr) break;
        Language lang;
        lang.code = b.code;
        lang.name = b.name;
        for (int i = 0; i < b.count; i++) {
            const char* why = refuse(b.pairs[i].english, b.pairs[i].text);
            if (why == nullptr) {
                lang.text[b.pairs[i].english] = b.pairs[i].text;
                lang.gameFont = lang.gameFont || pastLatin(b.pairs[i].text);
            }
        }
        st.languages.push_back(std::move(lang));
    }
    readPlayerCatalogs();
    int i = find(keep);
    if (keep != nullptr && keep[0] != '\0' && i < 0) {
        fprintf(stderr, "l10n: ui_language=%s: no such catalog; English\n", keep);
    }
    st.chosen = i < 0 ? 0 : i;
    st.serial++;
}

void init() {
    std::call_once(st.once, [] {
        settingsDescribe("ui_language", "ui_language: the settings menu's and the extras' language: en, ja, or the code "
                                        "of a catalog in the save folder's lang folder; unset: the build's (US en, JP ja)");
        const char* want = settingsGet("ui_language");
        load(want != nullptr && want[0] != '\0' ? want : BUILD_LANGUAGE);
        const char* log = SDL_getenv("XG_L10N_MISSING");
        if (log != nullptr && log[0] != '\0') st.missingLog = fopen(log, "a");
    });
}

} // namespace

extern "C" const char* l10nText(const char* english) {
    if (english == nullptr || english[0] == '\0') return english;
    init();
    if (st.chosen == 0) return english;
    const Language& lang = st.languages[(size_t)st.chosen];
    if (lang.gameFont && !skin::native()) return english; // no disc yet: ImGui's font has no kana
    const char* id = strstr(english, "##");
    std::string_view key = id ? std::string_view(english, (size_t)(id - english)) : std::string_view(english);
    auto it = lang.text.find(key);
    if (it == lang.text.end()) {
        if (st.missingLog != nullptr && !key.empty()) {
            std::lock_guard<std::mutex> guard(st.lock);
            if (st.missing.insert(std::string(key)).second) {
                std::string line(key);
                for (size_t k = 0; (k = line.find('\n', k)) != std::string::npos; k += 2) line.replace(k, 1, "\\n");
                fprintf(st.missingLog, "%s\n", line.c_str());
                fflush(st.missingLog);
            }
        }
        return english;
    }
    if (id == nullptr) return it->second;
    // A label with an id: the translation and the source's id suffix, kept
    // for the run (ImGui reads the text again next frame).
    std::lock_guard<std::mutex> guard(st.lock);
    std::string slot = lang.code + '\x1f' + english;
    auto found = st.labels.find(slot);
    if (found == st.labels.end()) found = st.labels.emplace(slot, std::string(it->second) + id).first;
    return found->second.c_str();
}

extern "C" int l10nLanguageCount(void) {
    init();
    return (int)st.languages.size();
}

extern "C" const char* l10nLanguageCode(int i) {
    init();
    return i >= 0 && i < (int)st.languages.size() ? st.languages[(size_t)i].code.c_str() : "";
}

extern "C" const char* l10nLanguageName(int i) {
    init();
    return i >= 0 && i < (int)st.languages.size() ? st.languages[(size_t)i].name.c_str() : "";
}

extern "C" int l10nLanguage(void) {
    init();
    return st.chosen;
}

extern "C" void l10nSetLanguage(int i) {
    init();
    if (i < 0 || i >= (int)st.languages.size()) return;
    if (i != st.chosen) st.serial++;
    st.chosen = i;
    settingsSet("ui_language", st.languages[(size_t)i].code.c_str());
    settingsSave();
}

extern "C" int l10nSerial(void) {
    init();
    return st.serial;
}

extern "C" void l10nReload(void) {
    init();
    std::string keep = st.languages[(size_t)st.chosen].code;
    std::lock_guard<std::mutex> guard(st.lock);
    load(keep.c_str());
}

extern "C" int l10nEntries(int i) {
    init();
    return i >= 0 && i < (int)st.languages.size() ? (int)st.languages[(size_t)i].text.size() : 0;
}
