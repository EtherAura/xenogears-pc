/*
 * Which engine runs (engine.c; docs/ENGINE_PLAN.md): the Original, the
 * bit-validated game from src/ (xenogears), or the Enhanced, built from the
 * fork in engine/ (xenogears-enhanced). Both executables share this platform
 * layer; the setting `engine` (XG_ENGINE for one run) names the one the
 * player wants, and whichever executable starts hands over to the other when
 * it is asked for and can be found.
 */
#ifndef XG_ENGINE_H
#define XG_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

enum { ENGINE_ORIGINAL = 0, ENGINE_ENHANCED = 1 };

int engineThis(void);              /* this executable's engine */
const char* engineName(int e);     /* "original" / "enhanced" (the setting's words) */
const char* engineTitle(int e);    /* "Original" / "Enhanced" */
int engineSetting(void);           /* the setting: ENGINE_*, or -1 when unset */
void engineSetSetting(int e);      /* set it (saved with the settings) */
/* That engine's executable, looked for beside this one (and, in a build run
 * from the repository, in the sibling build directories); NULL when none. */
const char* engineExecutable(int e);

/* A folder of packs or other data that lives beside the executable (name:
 * "portrait-packs"): beside this one, or beside the other engine's executable
 * when it is only there (the two engines share one installation). Writes the
 * first existing folder's path, else the one beside this executable, and
 * returns out. */
char* enginePackDir(const char* name, char* out, unsigned long size);

/* main: remember the arguments; then, when the setting asks for the other
 * engine and its executable exists, start it in this process's place with
 * the same arguments (never from a process that was started this way). Returns
 * only when staying with this engine; engineNote says why when the player asked
 * for the other one. */
void engineInit(int argc, char** argv);
void engineRedirect(void);
const char* engineNote(void);

/* The settings menu: save the settings and start engine e now (this process
 * ends; whatever was not saved in the game is lost). 0 when it cannot. */
int engineRestart(int e);

#ifdef __cplusplus
}
#endif

#endif
