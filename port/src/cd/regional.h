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
/* Field movies (src/field/f_800A55B8.c startFieldTransition): for a US Disc 2
 * scene the JP game plays without XA, its sound sequence (0xFF: none), with
 * Japanese cutscene audio; -1 otherwise. */
int pcRegionalFieldMovieSound(int field, int stream);
/* Before the movie op: the JP scene's silent score loads its WDS samples
 * and reverb through the normal field music loader; -1 preserves the cue. */
int pcRegionalFieldMovieMusic(int field, int stream);
const char* regionalSubtitle(void);
int regionalSubtitlesEnabled(void);
/* 0: off, 1: US dialogue, 2: English translation of Japanese dialogue. */
void regionalSetSubtitles(int mode);
#endif
