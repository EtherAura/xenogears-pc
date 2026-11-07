// F1 > System > the game's text (port/src/system/gametext.h, xg-htn.35.22):
// a source per category (dialogue, battle, menus, names and items), as the
// user chose: the disc's, the other release's (Japanese text on the US
// build, English on the JP build) or a text pack, with where it comes from;
// and packs made from the player's discs, to read, edit and use.

#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "skin.h"

extern "C" {
#include "system/gametext.h"
#include "system/settings.h"
}

namespace menu {

namespace {

enum { MAX_PACKS = 16 };
char packIds[MAX_PACKS][64], packNames[MAX_PACKS][128];
int packCount = -1;
char exportMessage[600];
bool exportFailed;
char imagePaths[2][1024];
bool imagePathsRead;

void readPacks() {
    packCount = gtPackList(packIds, packNames, MAX_PACKS);
}

// As the menu's own (sections.cpp): a box and its translated label.
bool checkbox(const char* label, bool* value) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = ImGui::Checkbox("##value", value);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", tr(label));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace

void gameTextRows() {
    static const char* const labels[GT_CATEGORIES] = {N_("Dialogue"), N_("Battle text"), N_("Game menus"),
                                                      N_("Names, items")};
    if (packCount < 0) readPacks();
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
        for (int p = 0; p < packCount && source == GT_SOURCE_PACK; p++) {
            if (!strcmp(packIds[p], gameTextPack(c))) now = 3 + p;
        }
        for (int i = 0; i < 3 + packCount; i++) {
            const char* name = i == 0 ? tr("Disc") : i == 1 ? other : i == 2 ? tr("Text image") : packNames[i - 3];
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
                else gameTextSetSource(c, GT_SOURCE_PACK, packIds[i - 3]);
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
        }
        ImGui::PopID();
    }
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
        ImGui::TextWrapped("%s", exportMessage);
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
