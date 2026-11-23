// F1 > System > the game's text (port/src/system/gametext.h, xg-htn.35.22):
// a source per category (dialogue, battle, menus, names and items), as the
// user chose: the disc's, the other release's (Japanese text on the US
// build, English on the JP build) or a text pack, with where it comes from;
// and packs made from the player's discs, to read, edit and use.

#include <algorithm>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "wrapped.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "skin.h"

extern "C" {
#include "cd/regional.h"
#include "system/gametext.h"
#include "system/settings.h"
}

namespace menu {

namespace {

struct Pack { std::string id, name; };
std::vector<Pack> packs;
bool packsRead;
char exportMessage[600];
bool exportFailed;
char imagePaths[2][1024];
bool imagePathsRead;

void readPacks() {
    packs.clear();
    gtVisitPacks([](void*, const char* id, const char* name) { packs.push_back({id, name}); }, nullptr);
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) { return a.id < b.id; });
    packsRead = true;
}

const char* setting(const char* key) {
    const char* value = settingsGet(key);
    return value ? value : "";
}

struct ImagePair {
    std::string name, paths[2];
    int mask = 0;
};
std::vector<ImagePair> images;
char imageName[128];
char imageMessage[256];
bool imagesRead;

std::string imageKey(int slot, const char* field) {
    return "text_library_" + std::to_string(slot) + "_" + field;
}

int validDiscs(const ImagePair& pair) {
    int mask = 0;
    for (int i = 0; i < 2; ++i) {
        if (pair.paths[i].empty()) continue;
        char why[256];
        GtImage* image = gtImageOpen(pair.paths[i].c_str(), why, sizeof(why));
        if (gtImageDisc(image) == i + 1) mask |= 1 << i;
        gtImageClose(image);
    }
    return mask;
}

bool samePaths(const ImagePair& a, const ImagePair& b) {
    return a.paths[0] == b.paths[0] && a.paths[1] == b.paths[1];
}

ImagePair currentPair() {
    ImagePair pair{tr("Text image"), {setting("text_image1"), setting("text_image2")}};
    const auto& path = pair.paths[0].empty() ? pair.paths[1] : pair.paths[0];
    if (!path.empty()) pair.name += " (" + path.substr(path.find_last_of("/\\") + 1) + ")";
    return pair;
}

void readImages() {
    images.clear();
    auto current = currentPair();
    for (int slot = 0; ; ++slot) {
        std::string name = setting(imageKey(slot, "name").c_str());
        if (name.empty()) break;
        ImagePair pair{name, {setting(imageKey(slot, "disc1").c_str()), setting(imageKey(slot, "disc2").c_str())}};
        pair.mask = validDiscs(pair);
        images.push_back(pair);
    }
    if (std::none_of(images.begin(), images.end(), [&](const ImagePair& p) { return samePaths(p, current); })) {
        current.mask = validDiscs(current);
        if (current.mask) images.push_back(current);
    }
    imagesRead = true;
}

void rememberImages() {
    ImagePair pair{imageName, {imagePaths[0], imagePaths[1]}};
    pair.mask = validDiscs(pair);
    for (int i = 0; i < 2; i++) {
        if (!pair.paths[i].empty() && !(pair.mask & (1 << i))) {
            snprintf(imageMessage, sizeof(imageMessage), tr("Check the path for Disc %d."), i + 1);
            return;
        }
    }
    if (!pair.mask || pair.name.empty()) return;
    int slot = 0;
    for (;; ++slot) {
        const char* name = setting(imageKey(slot, "name").c_str());
        if (!*name || pair.name == name) break;
    }
    settingsSet(imageKey(slot, "name").c_str(), imageName);
    settingsSet(imageKey(slot, "disc1").c_str(), imagePaths[0]);
    settingsSet(imageKey(slot, "disc2").c_str(), imagePaths[1]);
    settingsSave();
    imagesRead = false;
    snprintf(imageMessage, sizeof(imageMessage), "%s", tr("Saved to Language."));
}

void languageSelector() {
    if (!opt::row("Language", "text_dialogue text_battle text_menus text_names")) return;
    const bool us = !strcmp(gameTextOtherRegion(), "JP");
    const char* native = us ? tr("English") : tr("Japanese");
    const char* other = us ? tr("Japanese") : tr("English");
    const int source = gameTextLanguage();
    auto current = currentPair();
    std::string selected = tr("Custom");
    if (source == GT_SOURCE_DISC) selected = native;
    if (source == GT_SOURCE_OTHER) selected = other;
    if (source == GT_SOURCE_IMAGE) {
        if (!imagesRead && !opt::indexing()) readImages();
        selected = current.name;
        for (const auto& p : images) if (samePaths(p, current)) selected = p.name;
    }
    if (source == GT_SOURCE_PACK) {
        selected = gameTextPack(GT_DIALOGUE);
        for (const auto& p : packs) if (p.id == gameTextPack(GT_DIALOGUE)) selected = p.name;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##game-language", selected.c_str())) {
        static int otherMask;
        if (ImGui::IsWindowAppearing() || !imagesRead) {
            readPacks();
            readImages();
            otherMask = gameTextOtherDiscs();
        }
        skin::frameWindow();
        auto option = [](const char* label, bool active) {
            bool picked = ImGui::Selectable(label, active);
            if (active) ImGui::SetItemDefaultFocus();
            return picked;
        };
        if (option(native, source == GT_SOURCE_DISC)) gameTextSetLanguage(GT_SOURCE_DISC, nullptr);
        auto labelled = [](std::string name, int mask) {
            if (mask == 1) name += tr(" (Disc 1 only)");
            if (mask == 2) name += tr(" (Disc 2 only)");
            return name;
        };
        if (otherMask && option(labelled(other, otherMask).c_str(), source == GT_SOURCE_OTHER))
            gameTextSetLanguage(GT_SOURCE_OTHER, nullptr);
        ImGui::PushID("images");
        for (size_t i = 0; i < images.size(); ++i) {
            const auto& p = images[i];
            if (!p.mask) continue;
            ImGui::PushID((int)i);
            bool active = samePaths(current, p);
            if (option(labelled(p.name, p.mask).c_str(), source == GT_SOURCE_IMAGE && active)) {
                if (!active) {
                    settingsSet("text_image1", p.paths[0].c_str());
                    settingsSet("text_image2", p.paths[1].c_str());
                    imagePathsRead = false;
                }
                gameTextImageChanged();
                gameTextSetLanguage(GT_SOURCE_IMAGE, nullptr);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        for (const auto& p : packs) {
            ImGui::PushID(p.id.c_str());
            if (option(p.name.c_str(), source == GT_SOURCE_PACK && p.id == gameTextPack(GT_DIALOGUE)))
                gameTextSetLanguage(GT_SOURCE_PACK, p.id.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    hint("Changes dialogue, menus, names, battle labels and movie subtitles together. Reopen the game menu or enter a new battle to refresh its labels.");
    if (source >= 0 && source != GT_SOURCE_DISC && !regionalSubtitlesEnabled())
        hint("This source has no separate movie subtitle track.");
    if (source == GT_SOURCE_OTHER && regionalSubtitlesEnabled() == 2)
        hint("Movie subtitles use the English translation of the Japanese dialogue.");
}

// As the menu's own (sections.cpp): a box and its translated label.
bool checkbox(const char* label, bool* value) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = ImGui::Checkbox("##value", value);
    ImGui::SameLine();
    wrappedText("%s", tr(label));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace

void gameTextRows() {
    static const char* const labels[GT_CATEGORIES] = {N_("Dialogue"), N_("Battle text"), N_("Game menus"),
                                                      N_("Names, items")};
    if (!packsRead) readPacks();
    languageSelector();
    opt::group(N_("Individual text settings"), opt::COLLAPSED);
    // The other release's language, in this menu's.
    const char* other = !strcmp(gameTextOtherRegion(), "JP") ? tr("Japanese") : tr("English");
    for (int c = 0; c < GT_CATEGORIES; c++) {
        char rowKey[32];
        snprintf(rowKey, sizeof rowKey, "text_%s", gameTextCategoryKey(c));
        if (!opt::row(labels[c], rowKey)) continue; // the row translates its label
        ImGui::PushID(c);
        ImGui::PushID("gametext");
        // Disc, the other release, the text image, then the packs.
        int source = gameTextSource(c), now = 0;
        if (source == GT_SOURCE_OTHER) now = 1;
        if (source == GT_SOURCE_IMAGE) now = 2;
        for (int p = 0; p < (int)packs.size() && source == GT_SOURCE_PACK; p++) {
            if (!strcmp(packs[p].id.c_str(), gameTextPack(c))) now = 3 + p;
        }
        for (int i = 0; i < 3 + (int)packs.size(); i++) {
            const char* name = i == 0 ? tr("Disc") : i == 1 ? other : i == 2 ? tr("Text image") : packs[i - 3].name.c_str();
            if (i > 0) {
                ImGui::SameLine(0, 14 * scale());
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name).x) ImGui::NewLine();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::PushID(i);
            if (skin::selectable(name, now == i, 0, ImGui::CalcTextSize(name)) && i != now) {
                if (i == 0) gameTextSetSource(c, GT_SOURCE_DISC, nullptr);
                else if (i == 1) gameTextSetSource(c, GT_SOURCE_OTHER, nullptr);
                else if (i == 2) gameTextSetSource(c, GT_SOURCE_IMAGE, nullptr);
                else gameTextSetSource(c, GT_SOURCE_PACK, packs[i - 3].id.c_str());
                now = i;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::PopID();
        if (now != 0) {
            hint("%s", gameTextStatus(c));
            int given = 0, kept = 0;
            gameTextCounts(c, &given, &kept);
            if (given + kept > 0) hint("Since chosen: %d texts from it, %d kept the disc's.", given, kept);
            GtLayout layout;
            if (gameTextPackLayout(c, &layout)) {
                /* the pack's opt-ins (its pack.ini): the game's own look without them */
                if (layout.proportional) hint("The pack prints its text with proportional glyphs.");
                if (layout.windows == GT_WINDOWS_PACK) hint("The pack gives the dialogue windows' sizes.");
                if (layout.windows == GT_WINDOWS_GROW) hint("The dialogue windows grow to fit the pack's text.");
            }
            if (gameTextHasPictures(c)) {
                // the word pictures (Status, Attack...) from the same source: opt-in
                bool pictures = gameTextPictures(c);
                ImGui::PushID(c);
                if (checkbox("Pictures with text too", &pictures)) gameTextSetPictures(c, pictures);
                ImGui::PopID();
                hint("The word pictures (menu and battle labels) from the same source: its own, or the pack's "
                     "pictures. From the next menu or battle.");
            }
        }
        char key[32];
        snprintf(key, sizeof key, "text_%s", gameTextCategoryKey(c));
        overrideNote(key);
        if (c == GT_CATEGORIES - 1) { // (the four rows' common description: the last one's)
            hint("The game's own text from the other release's disc or a text pack, per kind: field and world map "
                 "dialogue, battle names, the menu's words, item and ability names. A text that does not fit (a few "
                 "fields' scripts differ between the releases) keeps the disc's. Applies to texts shown from now on.");
            hint("The other release's discs: text_other_disc1 and 2 in settings.ini, the Japanese discs of Audio (US "
                 "build), or images beside the game's.");
        }
    }

    // The text images: a translation patch the player applied to their own
    // image, read for its text only (system/gametext.h).
    opt::group(N_("Translation sources"), opt::COLLAPSED);
    if (opt::row("Text images", "text_image1 text_image2")) {
    if (!imagePathsRead) {
        for (int d = 0; d < 2; d++) {
            char key[32];
            snprintf(key, sizeof key, "text_image%d", d + 1);
            const char* v = settingsGet(key);
            snprintf(imagePaths[d], sizeof imagePaths[d], "%s", v ? v : "");
        }
        imagePathsRead = true;
    }
    for (int d = 0; d < 2; d++) {
        ImGui::PushID(100 + d);
        char label[32];
        snprintf(label, sizeof label, tr("Disc %d"), d + 1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        float use = ImGui::CalcTextSize(tr("Use")).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(-use);
        bool go = ImGui::InputText("##textimage", imagePaths[d], sizeof imagePaths[d], ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button(tr("Use"));
        if (go) {
            char key[32];
            snprintf(key, sizeof key, "text_image%d", d + 1);
            settingsSet(key, imagePaths[d]);
            settingsSave();
            gameTextImageChanged();
            imagesRead = false;
        }
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##translation-name", tr("Translation name"), imageName, sizeof(imageName));
    ImGui::BeginDisabled(!imageName[0] || (!imagePaths[0][0] && !imagePaths[1][0]));
    if (ImGui::Button(tr("Remember these images"))) rememberImages();
    ImGui::EndDisabled();
    if (imageMessage[0]) wrappedText("%s", imageMessage);
    hint("Use the same name to update a saved pair. You can also save a single disc.");
    hint("A translation patch (PPF, xdelta...) applied to your own image: the game keeps running from your verified "
         "image; the patched one is only read for its text, window sizes and font, chosen above as Text image. A "
         "patch's own code (a variable-width font, other window sizes) does not apply. Texts and fields the patch "
         "changed beyond text keep the disc's.");
    hint("With Pictures with text too, the patched image's word pictures as well.");
    overrideNote("text_image1");
    overrideNote("text_image2");
    } // Text images

    if (!opt::row("Text packs")) return;
    if (ImGui::Button(tr("Make a pack of this release's text"))) {
        char id[64];
        exportFailed = !gameTextExportPack(0, id, sizeof id, exportMessage, sizeof exportMessage);
        readPacks();
    }
    if (ImGui::Button(tr("Make a pack of the other release's text"))) {
        char id[64];
        exportFailed = !gameTextExportPack(1, id, sizeof id, exportMessage, sizeof exportMessage);
        readPacks();
    }
    if (ImGui::Button(tr("Make a pack of the text image's text"))) {
        char id[64];
        exportFailed = !gameTextExportPack(2, id, sizeof id, exportMessage, sizeof exportMessage);
        readPacks();
    }
    if (exportMessage[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, exportFailed ? skin::textError : skin::textGood);
        wrappedText("%s", exportMessage);
        ImGui::PopStyleColor();
    }
    hint("A pack is the game's text as UTF-8 files (field.txt, kernel.txt, menu.txt, battle.txt, world.txt) in %s, "
         "made from your own discs for your own use: edit it and choose it above. Tags such as <wait> and "
         "<name 0> stand for the game's codes; <g:N> for a glyph with no known character.",
         gtPackDir() != nullptr ? gtPackDir() : tr("the save folder's packs folder"));
    hint("A pack of the text image's text keeps the patch's font (font.bin; font.png shows its glyphs, font.map "
         "their characters): glyphs the patch added are <g:N> until font.map gives them a character. The pack is "
         "yours to edit; the image is no longer needed.");
    hint("A pack of this release's text has its word pictures too (pictures/, a PNG for each in its own size and "
         "colours): draw over them in the same places, then choose Pictures with text too.");
}

} // namespace menu
