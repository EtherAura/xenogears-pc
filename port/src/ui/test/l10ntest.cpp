// The interface's languages (l10n.h): the built-in Japanese catalog, a
// player's catalogs (one overriding the built-in, one a new language), the
// entries a catalog may not use (printf conversions that differ, fuzzy, an
// ImGui id), id suffixes, the English fallback without the game's font, and
// the missing-string log. Runs in the current directory (l10ntest/).

#include "ui/l10n.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool gameFont = true;
namespace skin {
bool native() { return gameFont; }
}

static int checks, failures;
#include "test/check.h"

static void write(const std::string& path, const char* text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "l10ntest: cannot write %s\n", path.c_str());
        std::exit(2);
    }
    std::fputs(text, f);
    std::fclose(f);
}

static int language(const char* code) {
    for (int i = 0; i < l10nLanguageCount(); i++)
        if (!std::strcmp(l10nLanguageCode(i), code)) return i;
    return -1;
}

int main() {
    SDL_setenv_unsafe("XG_SETTINGS", "none", 1); // language switching must never write player settings
    SDL_CreateDirectory("l10ntest");
    write("l10ntest/ja.po",
          "msgid \"\"\nmsgstr \"Language: ja\\n\"\n\n"
          "# a player's correction of a built-in entry\n"
          "msgid \"Display\"\nmsgstr \"表示\"\n");
    write("l10ntest/it.po",
          "msgid \"\"\nmsgstr \"\"\n\"Language: it\\n\"\n\"X-Language-Name: Italiano\\n\"\n\n"
          "msgid \"Audio\"\nmsgstr \"Suono\"\n\n"
          "#, c-format\nmsgid \"Slot %d: %s\"\nmsgstr \"Slot %s: %d\"\n\n"
          "#, fuzzy\nmsgid \"Back\"\nmsgstr \"Indietro\"\n\n"
          "msgid \"Cancel\"\nmsgstr \"Annulla##x\"\n\n"
          "#, c-format\nmsgid \"%d save%s, %d free block%s\"\nmsgstr \"%d salvataggi%.0s, %d blocchi liberi%.0s\"\n\n"
          "msgid \"Two \"\n\"lines\"\nmsgstr \"Due \"\n\"righe\"\n\n"
          "#~ msgid \"Old\"\n#~ msgstr \"Vecchio\"\n");
    std::remove("l10ntest/missing.txt");
    SDL_setenv_unsafe("XG_LANG_DIR", "l10ntest", 1);
    SDL_setenv_unsafe("XG_UI_LANGUAGE", "ja", 1);
    SDL_setenv_unsafe("XG_L10N_MISSING", "l10ntest/missing.txt", 1);

    // English, the built-in Japanese and the player's Italian.
    int ja = language("ja"), it = language("it");
    EXPECT(l10nLanguageCount() == 3);
    EXPECT(language("en") == 0 && ja > 0 && it > 0);
    EXPECT(!std::strcmp(l10nLanguageName(it), "Italiano"));
    EXPECT(!std::strcmp(l10nLanguageName(ja), "日本語"));
    EXPECT(l10nLanguage() == ja);
    EXPECT(l10nEntries(ja) > 600);

    // Japanese: built-in, the player's correction over it, id suffixes kept.
    EXPECT(!std::strcmp(tr("Audio"), "サウンド"));
    EXPECT(!std::strcmp(tr("Speedup"), "高速化"));
    EXPECT(!std::strcmp(tr("Reduces card/loading waits and field/battle stalls, and enables smooth field motion at 60 Hz by default. Gameplay, animation events, movies and audio stay at normal speed. Off keeps original timing and frame cadence."),
                       "カード・読み込み・フィールド・戦闘の待ち時間を減らし、標準で60 Hzのなめらかなフィールド表示を有効にします。ゲーム進行・アニメのイベント・ムービー・音声の速度は変わりません。オフで原作のタイミングとフレーム周期に戻ります。"));
    EXPECT(!std::strcmp(tr("Display"), "表示"));
    EXPECT(!std::strcmp(tr("Back##help"), "もどる##help"));
    EXPECT(!std::strcmp(tr("Back###stable"), "もどる###stable"));
    EXPECT(tr("Back##help") == tr("Back##help")); // the same text for the run
    const char* unknown = "No such text in any catalog";
    EXPECT(tr(unknown) == unknown);
    EXPECT(tr("") != nullptr && tr("")[0] == '\0');
    EXPECT(tr(nullptr) == nullptr);
    char buf[128];
    std::snprintf(buf, sizeof buf, tr("%d save%s, %d free block%s"), 2, "s", 13, "s");
    EXPECT(!std::strcmp(buf, "2 件、空き 13 ブロック"));

    // Without the game's font (no disc yet) kana cannot be drawn: English.
    gameFont = false;
    EXPECT(!std::strcmp(tr("Audio"), "Audio"));
    gameFont = true;

    // Italian: what it may use, and what falls back to English.
    int serial = l10nSerial();
    l10nSetLanguage(it);
    EXPECT(l10nSerial() != serial);
    EXPECT(!std::strcmp(tr("Audio"), "Suono"));
    EXPECT(!std::strcmp(tr("Slot %d: %s"), "Slot %d: %s")); // conversions differ
    EXPECT(!std::strcmp(tr("Back"), "Back"));               // fuzzy
    EXPECT(!std::strcmp(tr("Cancel"), "Cancel"));           // an id in the translation
    EXPECT(!std::strcmp(tr("Old"), "Old"));                 // obsolete
    EXPECT(!std::strcmp(tr("Two lines"), "Due righe"));
    std::snprintf(buf, sizeof buf, tr("%d save%s, %d free block%s"), 2, "s", 13, "s");
    EXPECT(!std::strcmp(buf, "2 salvataggi, 13 blocchi liberi"));
    gameFont = false;
    EXPECT(!std::strcmp(tr("Audio"), "Suono")); // Latin: ImGui's own font draws it
    gameFont = true;

    // A reload keeps the chosen language and the texts handed out.
    const char* before = tr("Audio");
    write("l10ntest/it.po", "msgid \"\"\nmsgstr \"Language: it\\n\"\n\nmsgid \"Audio\"\nmsgstr \"Musica\"\n");
    l10nReload();
    EXPECT(l10nLanguage() == language("it"));
    EXPECT(!std::strcmp(tr("Audio"), "Musica"));
    EXPECT(!std::strcmp(before, "Suono"));

    // English: the sources' own strings.
    l10nSetLanguage(0);
    EXPECT(!std::strcmp(tr("Audio"), "Audio"));
    const char* lit = "Display";
    EXPECT(tr(lit) == lit);

    // The missing log: each English string without a translation, once.
    FILE* f = std::fopen("l10ntest/missing.txt", "rb");
    EXPECT(f != nullptr);
    if (f) {
        char text[4096] = {0};
        size_t n = std::fread(text, 1, sizeof text - 1, f);
        std::fclose(f);
        text[n] = '\0';
        EXPECT(std::strstr(text, unknown) != nullptr);
        int count = 0;
        for (const char* p = text; (p = std::strstr(p, unknown)) != nullptr; p++) count++;
        EXPECT(count == 1);
    }

    std::printf("l10ntest: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
