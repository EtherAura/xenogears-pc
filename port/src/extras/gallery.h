#ifndef PORT_EXTRAS_GALLERY_H
#define PORT_EXTRAS_GALLERY_H

/*
 * The gallery's C side (xg-htn.6): what gallery.cpp shows comes from here.
 *
 *   gallery_movie.c  movies played by the game's own video player
 *   gallery_scan.c   the movie list and the scene list, recovered at run time
 *                    from the disc: the movie streams' own frame headers, and
 *                    the field scripts' movie and story-progress operations
 *
 * Everything is read from the player's disc when the gallery opens; nothing
 * of it is kept in the repository.
 */

#include <stdint.h>

#include "disc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- movies (gallery_movie.c) ----------------------------------------------------------------- */

/*
 * A movie player: the game's own STR player (the video_player overlay's
 * initStrPlayer, startStrPlayback, updateStrPlayer, stopStrPlayback and
 * freeStrPlayer, as the movie mode and the field call them) run in
 *   - a code sandbox (sandbox.h) of its own: the overlay's and main's data, a
 *     heap, and a GPU whose VRAM gets the decoded slices (LoadImage);
 *   - a world of its own (port/src/system/world.h): its clock and interrupts,
 *     and an SPU whose output comes here;
 *   - a drive of its own with the disc image (cd.h cdWorldCreate): LIBCD's
 *     streaming fills the player's ring at the drive's speed, and the XA
 *     sectors go to the world's SPU through its CD input, as on the PS1;
 *   - an MDEC of its own (mdec.h mdecWorldCreate).
 * The sound driver's CD volume fade (setCdVolume, which the player calls at
 * a movie's start and end) is stepped as its 240 Hz tick steps it.
 * The paused game's drive, MDEC, SPU and memory are never touched.
 */
typedef struct XgMovie XgMovie;

XgMovie* xgMovieCreate(XgDisc* disc, char* why, int whySize);
void xgMovieDestroy(XgMovie* m);

/* Start movie stream `stream` (the game's movie number: directory 0x18/1,
 * file stream + 2) at sector `offset` of it, from frame `first` up to frame
 * `last` (the player stops after it), with its XA audio (file 1, channel 1),
 * in 24-bit colour, as the field's movie ops start them; its frames are
 * width x height (the stream's headers say). 1 on success. */
int xgMovieStart(XgMovie* m, int stream, int offset, int first, int last, int width, int height);
void xgMovieStop(XgMovie* m);
int xgMoviePlaying(const XgMovie* m); /* started and not at its end */

/* Let `vsyncs` vertical blanks of the movie's time pass (the player updated
 * three times each, as the game's movie loops do). The audio of that time
 * goes to the movie's audio stream when `audible`. */
void xgMovieRun(XgMovie* m, int vsyncs, int audible);
/* The movie's audio stream: what is queued, in seconds (-1: no audio
 * device); and silence. */
double xgMovieAudioQueued(void);
void xgMovieAudioStop(void);

/* The newest whole frame: its number (0: none yet), and its pixels as RGBA
 * (rgba: room for 320 x 240) with its size. */
int xgMovieFrame(XgMovie* m, uint32_t* rgba, int* w, int* h);

/* The audio of the last xgMovieRun calls is also kept (for WAV export) when
 * recording: on starts empty, off stops; the frames kept (44.1 kHz stereo). */
void xgMovieRecord(XgMovie* m, int on);
const int16_t* xgMovieRecorded(const XgMovie* m, int* frames);

/* ---- what the disc holds (gallery_scan.c) ---------------------------------------------------- */

#define XG_MOVIE_RATE 44100

/* A movie: a run of frames numbered from 1 in one of a disc's movie streams
 * (a stream holds one or several), found in the stream's own frame headers. */
typedef struct {
    int disc;       /* 1 or 2 */
    int stream;     /* the game's movie number (directory 0x18/1, file stream + 2) */
    int lba;        /* the stream's first sector */
    int offset;     /* the movie's first sector in the stream */
    int sectors;    /* its sectors */
    int frames;     /* its last frame number */
    int width, height;
} XgMovieEntry;

/* Where the game plays a movie: a field/battle-event op, or the kernel. */
typedef struct {
    int disc;       /* the disc it plays from */
    int stream, offset, first, last;
    int field;      /* field number; -1: opening; -2: battle event */
    int op;         /* field 0xA0/0x60/0x67, battle 0x27, kernel 0 */
    int mode;       /* field mode/sound sequence, or battle movie type | 0x80 */
    int movie;      /* the XgMovieEntry it plays (an index), -1 if none */
    int event, pc;  /* for battle events: event number and bytecode offset */
} XgMovieUse;

/* A scene: an indexed SET_GAMEPROGRESS and an inferred start. Readiness is
 * separate: gallery_scene.h provides only presets verified by real replay. */
typedef struct {
    int field;
    int setsTo;     /* the story progress value the scene sets */
    int progress;   /* inferred progress before the indexed operation */
    int entry;      /* the entrance (gameState 0x2320, the field's variable 2) */
    int disc;       /* the disc the scene plays from */
    int use;        /* a movie the field plays (an XgMovieUse index), -1 */
    int actor, slot; /* the script routine that sets the progress */
    int start, pc;  /* its entry point and the op's place in the field's code */
} XgScene;

/* The scan of the discs, a step at a time (the UI shows its progress): the
 * movie streams of each disc (their frame headers, by binary search), then
 * every field's script (the game's own LZSS decoder; walked from the routine
 * table along jumps and calls, with the instruction lengths of the field
 * VM's handlers): its movie ops (checked against the streams) and its story
 * progress ops, then battle-event movie requests from 20/02.
 * Discs: the drive's two (disc 2 may be NULL). */
typedef struct XgGalleryScan XgGalleryScan;
XgGalleryScan* xgScanBegin(XgDisc* disc1, XgDisc* disc2);
void xgScanFree(XgGalleryScan* s);
/* Do about `budget` milliseconds of work; 1 when the scan is complete. */
int xgScanStep(XgGalleryScan* s, int budget);
double xgScanProgress(const XgGalleryScan* s); /* 0..1 */
const char* xgScanDoing(const XgGalleryScan* s);

int xgScanMovieCount(const XgGalleryScan* s);
const XgMovieEntry* xgScanMovie(const XgGalleryScan* s, int i);
int xgScanUseCount(const XgGalleryScan* s);
const XgMovieUse* xgScanUse(const XgGalleryScan* s, int i);
int xgScanSceneCount(const XgGalleryScan* s);
const XgScene* xgScanScene(const XgGalleryScan* s, int i);
/* Field archives on a disc (1, 2), and whether field n's is real there (not
 * a placeholder). */
int xgScanFieldCount(const XgGalleryScan* s, int disc);
int xgScanFieldReal(const XgGalleryScan* s, int disc, int field);

/* The first field whose script shows event picture n (04/7FB+n, OPX_77),
 * -1 if none (the town maps are the field overlay's own). */
int xgScanPictureField(const XgGalleryScan* s, int picture);

/* The sector offset in movie i's stream where frame `frame` starts (frame
 * header sector 0), for seeking; -1 if there is no such frame. */
int xgScanSeek(XgGalleryScan* s, int movie, int frame);

/* ---- field scripts (gallery_scan.c) ---------------------------------------------------------- */

/* A field's script (archive 04/B8+2n, section 5, decompressed by the game's
 * own LZSS decoder): its routine table (32 entry points per actor) and code. */
typedef struct {
    uint8_t* code; /* malloc'd */
    int codeSize;
    int actors;
    uint16_t (*routines)[32];
} XgFieldScript;

/* 1 and *out filled; 0 if the field has no script on this disc (a
 * placeholder, a missing file). */
int xgFieldScriptRead(XgDisc* disc, int field, XgFieldScript* out);
void xgFieldScriptFree(XgFieldScript* s);

/* Pure reachable-instruction walker and media decoder; returned storage is
 * malloc'd. A media request retains the field and exact code PC even when
 * its arguments cannot be resolved without executing game state. */
int xgFieldOpLength(const uint8_t* code, int size, int pc);
int xgFieldJumpOffset(int op, int extended);
int xgFieldEndsFlow(int op);
uint8_t* xgFieldScriptWalk(const XgFieldScript* s);
enum { XG_MEDIA_MOVIE, XG_MEDIA_PICTURE };
enum { XG_MEDIA_LITERAL, XG_MEDIA_DYNAMIC, XG_MEDIA_INVALID };
typedef struct {
    int kind, problem, field, pc, op;
    int stream, offset, first, last, mode, picture;
} XgFieldMedia;
int xgFieldMediaDecode(const XgFieldScript* s, int field, XgFieldMedia** out);
int xgFieldMediaRead(XgDisc* disc, int field, XgFieldMedia** out); /* -1 unreadable, 0 no refs */
int xgFieldMediaMovie(XgDisc* disc, const XgFieldMedia* ref, XgMovieEntry* out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_GALLERY_H */
