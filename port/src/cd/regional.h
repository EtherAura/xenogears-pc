#ifndef PORT_CD_REGIONAL_H
#define PORT_CD_REGIONAL_H
#include <stddef.h>
#include <stdint.h>
/* Original runtime regional options. Images are always opened read-only. */
void regionalInit(void);
int regionalBattleEnabled(void); /* fixed at launch: allocations cannot change mid-load */
int regionalMovieEnabled(void);
void regionalSetBattle(int on);
void regionalSetMovie(int on);
const char* regionalSourcePath(int disc);
const char* regionalSourceStatus(int disc);
int regionalSetSource(int disc, const char* path, char* why, size_t size);
/* Absolute FAT index. Return 0 for an unchanged file, else the safe allocation
 * size. copy==NULL is a size query; otherwise copies that many bytes. */
int pcRegionalFile(int index, const unsigned char* table, const unsigned short* dirs, void* copy);
int pcRegionalBatch(void* commands, int first, const unsigned char* table, const unsigned short* dirs);
/* The player's video remains in the drive; only XA payloads are replaced. */
void regionalMovieBegin(int lba);
void regionalMovieSector(int lba, unsigned char raw[2352]);
void regionalMovieEnd(void);
void regionalMovieFrame(int frame);
/* The movie the captions go by, as regionalMovieBegin found it and
 * regionalMovieFrame last told: out = {Japanese disc, disc number, clip (the
 * movie's number in the movie directory), the clip's first sector, frame}.
 * 0 (and out all 0) when no movie plays. */
int regionalMovieIdentity(int out[5]);
/* Field movies (src/field/f_800A55B8.c startFieldTransition): for a US Disc 2
 * scene the JP game plays without XA, its sound sequence (0xFF: none), with
 * Japanese cutscene audio; -1 otherwise. */
int pcRegionalFieldMovieSound(int field, int stream);
/* Before the movie op: the JP scene's silent score loads its WDS samples
 * and reverb through the normal field music loader; -1 preserves the cue. */
int pcRegionalFieldMovieMusic(int field, int stream);
/* Subtitles (F1 > Audio; settings.ini). Each applies only with a track on:
 *   cutscene_subtitles          0 off, 1 US dialogue, 2 Japanese translation, 5 text pack
 *                               (legacy 3 or 4: track 1 or 2 with all sounds)
 *   cutscene_sound_captions     0 none, 1 voices ([screams]), 2 all sounds
 *   cutscene_lyrics             1: the songs' lyrics
 *   cutscene_invented_language  lines in the game's invented language:
 *                               0 the English script's spelling, 1 as spoken
 *                               (the Japanese track)
 * The extras' movie viewer chooses its own track and sounds; lyrics and the
 * invented language follow these settings. */
/* The build carries the caption text (the local subtitles.inc: it
 * transcribes the game's dialogue, so public builds leave it out). */
int regionalCaptionsAvailable(void);
enum { REGIONAL_PACK_SUBTITLES = 3 };
typedef const char* (*RegionalPackCaption)(int japanese, int disc, int clip, int frame);
void regionalSetPackCaptionReader(RegionalPackCaption reader);
int regionalSubtitlesEnabled(void); /* 0 off, 1 US, 2 Japanese translation, 3 active text pack */
void regionalSetSubtitles(int track);
int regionalSoundCaptions(void);
void regionalSetSoundCaptions(int level);
int regionalLyrics(void);
void regionalSetLyrics(int on);
int regionalInventedSpoken(void);
void regionalSetInventedSpoken(int on);
/* The caption at the current movie's frame for the settings; NULL: none. */
const char* regionalSubtitle(void);
/* The same for a track (1 US dialogue, 2 Japanese translation, 3 text pack)
 * and sound level (0-2) chosen by the caller, e.g. the extras' movie viewer,
 * whose movie is its CD world's (cd.h cdWorldSwap carries this file's
 * state). regionalCaption: no sounds. Several captions at once come as one
 * line each (sounds first, then the lyric, then the dialogue) in a buffer
 * the next call for that track and level reuses: copy it to keep it. */
const char* regionalCaption(int track);
const char* regionalCaptionWith(int track, int sounds);
#endif
