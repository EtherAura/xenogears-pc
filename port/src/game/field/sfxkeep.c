/*
 * Original-game bug fixes: a field's looping sound effect outlasts a menu.
 *
 * Field scripts play sound effects on eight channels (OP_C4SubSub1: channel
 * c is sound effect tracks (2c ^ 8) and the next). The menus play theirs
 * with playSoundEffect on tracks numMaxSoundEffectInstances - 2 and the next
 * (14 and 15): field channel 3. A looping ambience a script started there
 * (South Bledavik's market crowd, field 103: OPX_65 sound 409 on channel 3)
 * is cut by the menu's first sound, and nothing starts it again: the
 * field's scripts do not rerun after a menu. With the option on,
 * openFieldMenu notes the channels whose looping effect (a track with an
 * infinite-loop point, seqOP_11) still plays as the script started it, with
 * its tracks' volume and pan at that moment, and starts those the menu ended
 * again, at that volume and pan, once the field is back.
 */
#include "common.h"
#include "main/sound.h"
#include "pc_gameplay.h"

extern SoundInstance* pSoundEffectsInstances;
void StopSoundEffect(s32 ch);
void PlaySoundEffect2(s32 id, s32 ch, s32 vol, s32 pan);

static struct {
    s32 id;
    s16 volume;
    s16 pan;
    s16 trackVolume[2]; /* as the menu opened: a script may have changed them */
    s16 trackPan[2];    /* since (OPX_62/OPX_63, positional sounds) */
} fieldSfx[8];

void pcFieldSfxPlayed(int id, int volume, int pan, int channel) {
    channel &= 7;
    fieldSfx[channel].id = id;
    fieldSfx[channel].volume = volume;
    fieldSfx[channel].pan = pan;
}

/* Channel's looping effect, as the script started it, still plays. */
static s32 fieldSfxLooping(s32 channel) {
    SoundTrack* t;
    s32 i;

    if (pSoundEffectsInstances == NULL || fieldSfx[channel].id == 0) {
        return 0;
    }
    t = &pSoundEffectsInstances->m94_tracks[(channel * 2 & 0xFE) ^ 8];
    for (i = 0; i < 2; i++, t++) {
        if ((t->m0_flags & 1) && t->m8.m8_id == fieldSfx[channel].id && t->m18_loopStart != NULL) {
            return 1;
        }
    }
    return 0;
}

unsigned int pcFieldSfxLoopingMask(void) {
    unsigned int mask = 0;
    s32 c;

    if (!pcGameplayOriginalBugFixes()) {
        return 0;
    }
    for (c = 0; c < 8; c++) {
        if (fieldSfxLooping(c)) {
            SoundTrack* t = &pSoundEffectsInstances->m94_tracks[(c * 2 & 0xFE) ^ 8];
            s32 i;

            for (i = 0; i < 2; i++) {
                fieldSfx[c].trackVolume[i] = t[i].m76_volume;
                fieldSfx[c].trackPan[i] = t[i].m74_pan;
            }
            mask |= 1 << c;
        }
    }
    return mask;
}

/* Start again, as the script did, the effects of mask that no longer play. */
void pcFieldSfxRestore(unsigned int mask) {
    s32 c;

    for (c = 0; c < 8; c++) {
        if ((mask & (1 << c)) && !fieldSfxLooping(c)) {
            SoundTrack* t = &pSoundEffectsInstances->m94_tracks[(c * 2 & 0xFE) ^ 8];
            s32 i;

            StopSoundEffect(c * 2);
            PlaySoundEffect2(fieldSfx[c].id, c * 2, fieldSfx[c].volume, fieldSfx[c].pan);
            for (i = 0; i < 2; i++) {
                if (t[i].m0_flags & 1) { /* as setSoundEffectChannelVolume/Pan */
                    t[i].m76_volume = fieldSfx[c].trackVolume[i];
                    t[i].m74_pan = fieldSfx[c].trackPan[i];
                    t[i].m2_update |= 0x100;
                }
            }
        }
    }
}
