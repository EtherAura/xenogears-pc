/* Xenogears saves on the memory cards (saves.h): which blocks hold one, and
 * what a save says about itself. Reads the card images only. */

#include "saves.h"

#include <stdio.h>
#include <string.h>

#include "card.h"
#include "common.h"
#include "main/game_state.h"
#include "menu/menu.h"

/* The offsets saves.h names, from the game's own layouts. */
_Static_assert(offsetof(MenuSaveHeader, m100_summary) == CARD_SAVE_DATA, "the save data's start");
_Static_assert(CARD_SAVE_NAMES == CARD_SAVE_DATA + offsetof(MenuSaveSummary, m24_characterNames), "names");
_Static_assert(CARD_SAVE_PARTY == CARD_SAVE_DATA + offsetof(MenuSaveGame, m290_party), "character records");
_Static_assert(CARD_SAVE_ABILITIES == CARD_SAVE_DATA + offsetof(MenuSaveGame, mEC4_abilities), "ability rows");
_Static_assert(CARD_SAVE_STATE == CARD_SAVE_DATA + offsetof(MenuSaveGame, m1124_gameStateTail), "the state's tail");
_Static_assert(sizeof(((MenuSaveGame*)0)->m1124_gameStateTail) == CARD_SAVE_STATE_BYTES, "the tail's size");
_Static_assert(CARD_SAVE_STATE_FROM + CARD_SAVE_STATE_BYTES == sizeof(GameState), "the tail runs to the state's end");
_Static_assert(CARD_SAVE_DATA + sizeof(MenuSaveGame) <= 0x1F00, "the save data ends before the free tail");
_Static_assert(sizeof(GameStateCharacter) == 0xA4 && sizeof(GameStateSize20) == 0x20, "the PS1's record sizes");

static unsigned rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int cardGameRegion(void) {
#if VERSION_JP
    return 'J';
#else
    return 'U';
#endif
}

static int regionOfName(const char* name) {
    if (!strncmp(name, "BASLUS-00664", 12)) return 'U';
    if (!strncmp(name, "BISLPS-01160", 12)) return 'J';
    return 0;
}

static int checksumOk(const uint8_t* b) {
    unsigned sum = 0;
    for (int k = 0x100; k < 0x1FFF; k++) sum += b[k];
    return (sum & 0xFF) == b[0x1FFF];
}

/* "XENOGEARS/No.12 Into Solaris": the number and what follows it. */
static void splitTitle(CardGameSave* s) {
    const char* t = s->title;
    const char* no = strstr(t, "No.");
    s->number = 0;
    s->location[0] = '\0';
    if (no == NULL) {
        snprintf(s->location, sizeof(s->location), "%s", t);
    } else {
        const char* p = no + 3;
        while (*p >= '0' && *p <= '9') s->number = s->number * 10 + (*p++ - '0');
        while (*p == ' ') p++;
        snprintf(s->location, sizeof(s->location), "%s", p);
    }
    size_t n = strlen(s->location);
    while (n > 0 && s->location[n - 1] == ' ') s->location[--n] = '\0';
}

int cardGameSaveRead(const uint8_t* b, CardGameSave* s) {
    if (!checksumOk(b)) return 0;
    s->progress = (int)rd16(b + CARD_SAVE_AT(offsetof(GameState, m1930_fieldVarsBackup)));
    s->disc = rd16(b + CARD_SAVE_AT(offsetof(GameState, m1930_fieldVarsBackup) + 2 * 82)) ? 2 : 1;
    s->playTime = rd32(b + CARD_SAVE_DATA + offsetof(MenuSaveSummary, m0_playTime));
    s->id = memcmp(b + 0x1F00, "XGID", 4) == 0 ? rd32(b + 0x1F04) : 0;
    s->joined = (int)rd16(b + CARD_SAVE_AT(offsetof(GameState, m1D30_partyMemberBitField))) & 0x7FF;
    for (int i = 0; i < 3; i++) s->party[i] = b[CARD_SAVE_AT(offsetof(GameState, m1D34_currentParty)) + i];
    for (int c = 0; c < CARD_SAVE_MEMBERS; c++) {
        const uint8_t* r = b + CARD_SAVE_PARTY + c * sizeof(GameStateCharacter);
        CardSaveMember* m = &s->members[c];
        m->level = r[offsetof(GameStateCharacter, m62_Level)];
        m->hp = (int)rd16(r + offsetof(GameStateCharacter, m4C_HP));
        m->maxHp = (int)rd16(r + offsetof(GameStateCharacter, m4E_MaxHP));
        m->ep = (int)rd16(r + offsetof(GameStateCharacter, m50_MP));
        m->maxEp = (int)rd16(r + offsetof(GameStateCharacter, m52_MaxMP));
    }
    return 1;
}

int cardGameSaves(CardGameSave* out, int max, int region) {
    int n = 0;
    for (int slot = 0; slot < 2; slot++) {
        if (!cards[slot].present || !cardIsFormatted(cards[slot].data)) continue;
        CardSave saves[CARD_BLOCKS];
        int count = cardListSaves(cards[slot].data, saves, CARD_BLOCKS);
        for (int i = 0; i < count && n < max; i++) {
            int r = regionOfName(saves[i].name);
            if (!r || (region && r != region) || saves[i].block < 1 || saves[i].block >= CARD_BLOCKS) continue;
            CardGameSave* s = &out[n];
            memset(s, 0, sizeof(*s));
            if (!cardGameSaveRead(cards[slot].data + saves[i].block * CARD_BLOCK, s)) continue;
            s->card = slot;
            s->block = saves[i].block;
            s->region = r;
            snprintf(s->title, sizeof(s->title), "%s", saves[i].title);
            splitTitle(s);
            n++;
        }
    }
    return n;
}

const uint8_t* cardGameSaveBlock(const CardGameSave* s) {
    if (s == NULL || s->card < 0 || s->card > 1 || s->block < 1 || s->block >= CARD_BLOCKS) return NULL;
    const Card* c = &cards[s->card];
    if (!c->present || !cardIsFormatted(c->data)) return NULL;
    const uint8_t* f = c->data + s->block * CARD_FRAME;
    char name[CARD_NAME_MAX + 1];
    memcpy(name, f + 0x0A, CARD_NAME_MAX);
    name[CARD_NAME_MAX] = '\0';
    if (f[0] != DIR_FIRST || regionOfName(name) != s->region) return NULL;
    const uint8_t* b = c->data + s->block * CARD_BLOCK;
    return checksumOk(b) ? b : NULL;
}
