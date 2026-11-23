// textWrapCut (ui/textwrap.h, xg-em4y): Japanese lines break between characters,
// never before 、。」 small kana or ー, never after 「（; spaces and long words as before.
// Widths here: ASCII 1, everything else 2 (half and full width).
#include "ui/textwrap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int checks, failed;

static void require(bool good, const char* what) {
    ++checks;
    if (!good) {
        ++failed;
        std::fprintf(stderr, "textwrap check %d failed: %s\n", checks, what);
    }
}

static float advance(unsigned c, void*) {
    return c < 0x80 ? 1.0f : 2.0f;
}

// The lines textLines would draw (its loop: spaces after a cut skipped, '\n' ends a line).
static std::vector<std::string> wrap(const char* text, float width) {
    std::vector<std::string> out;
    const char* end = text + std::strlen(text);
    for (const char* p = text; p < end;) {
        const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        const char* lineEnd = nl != nullptr ? nl : end;
        if (p == lineEnd) out.push_back("");
        while (p < lineEnd) {
            const char* cut = textWrapCut(p, lineEnd, width, advance, nullptr);
            if (cut <= p) {
                std::fprintf(stderr, "textwrap: no progress\n");
                std::exit(1);
            }
            out.emplace_back(p, cut);
            p = cut;
            while (p < lineEnd && *p == ' ') p++;
        }
        p = nl != nullptr ? nl + 1 : end;
    }
    return out;
}

static std::string joined(const std::vector<std::string>& lines) {
    std::string s;
    for (size_t i = 0; i < lines.size(); i++) s += (i ? "|" : "") + lines[i];
    return s;
}

static void expect(const char* text, float width, const char* want) {
    std::string got = joined(wrap(text, width));
    char what[512];
    std::snprintf(what, sizeof what, "\"%s\" at %g: got \"%s\", want \"%s\"", text, width, got.c_str(), want);
    require(got == want, what);
}

static bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

static bool endsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

int main() {
    // The stop does not begin a line: the character before it goes along.
    expect("あいうえお。かきくけこ", 10, "あいうえ|お。かきく|けこ");
    expect("あいうえ、おかきくけこ", 8, "あいう|え、おか|きくけこ");
    // Brackets: 「 does not end a line, 」 does not begin one.
    expect("あいう「えお」かき", 8, "あいう|「えお」|かき");
    expect("あ「いう」", 6, "あ「い|う」"); // 「 stays with い
    // Small kana and the long-vowel mark stay with what comes before.
    expect("あいうきょう", 8, "あいう|きょう");
    expect("あいうえコーヒー", 10, "あいうえ|コーヒー");
    // An ASCII word inside Japanese breaks before or after it, not stranded:
    // "ゲームはGitHubの" fits 14 to the end of GitHub.
    expect("ゲームはGitHubの最新", 14, "ゲームはGitHub|の最新");
    expect("ゲームはGitHubの最新", 10, "ゲームは|GitHubの最|新");
    // Spaces still break, and are left out at the cut.
    expect("あい GitHub うえ", 10, "あい|GitHub う|え");
    // A word wider than the line is cut where it overflows; a line holds one character at least.
    expect("abcdefghij", 4, "abcd|efgh|ij");
    expect("あい", 1, "あ|い");
    // Line ends in the text.
    expect("あい\nうえ", 20, "あい|うえ");
    expect("あ\n\nい", 20, "あ||い");
    // ASCII alone: words at spaces.
    expect("hello world foo", 11, "hello world|foo");
    require(!textHasCjk("hello, world", "hello, world" + 12), "ASCII is not CJK");
    const char* ja = "設定";
    require(textHasCjk(ja, ja + std::strlen(ja)), "kanji are CJK");

    // Every width for the question's Japanese text (the case that showed the bug): no line
    // begins with a stop, a closing bracket or a small kana, none ends on an opening bracket,
    // and none is wider than the width unless it is one character.
    const char* question = "ゲームは GitHub に最新リリースの小さな署名付きファイルを尋ねます。あなたについての情報は"
                           "送られません。新しいバージョンは画面の隅と「F1、システム、アップデート」に表示され、"
                           "そこで変更できます。";
    const char* banned[] = {"、", "。", "」", "ー", "ょ", "ッ", "）"};
    for (int w = 4; w <= 60; w++) {
        auto lines = wrap(question, (float)w);
        for (size_t i = 0; i < lines.size(); i++) {
            const std::string& l = lines[i];
            float used = 0;
            int chars = 0;
            for (size_t k = 0; k < l.size();) {
                unsigned char b = (unsigned char)l[k];
                size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : 4;
                used += b < 0x80 ? 1.0f : 2.0f;
                chars++;
                k += n;
            }
            char what[256];
            std::snprintf(what, sizeof what, "width %d line %zu \"%s\" fits", w, i, l.c_str());
            require(used <= w || chars == 1 || (!l.empty() && l.back() == ' '), what);
            if (i > 0 && w >= 8) {
                for (const char* b : banned) {
                    std::snprintf(what, sizeof what, "width %d line %zu \"%s\" does not begin with %s", w, i, l.c_str(), b);
                    require(!startsWith(l, b), what);
                }
            }
            std::snprintf(what, sizeof what, "width %d line %zu \"%s\" does not end with 「", w, i, l.c_str());
            require(i + 1 == lines.size() || !endsWith(l, "「"), what);
        }
        std::string all;
        for (auto& l : lines) all += l;
        std::string want;
        for (const char* p = question; *p; p++) if (*p != ' ') want += *p;
        std::string got;
        for (char c : all) if (c != ' ') got += c;
        char what[64];
        std::snprintf(what, sizeof what, "width %d keeps every character", w);
        require(got == want, what);
    }
    std::printf("textwraptest: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
