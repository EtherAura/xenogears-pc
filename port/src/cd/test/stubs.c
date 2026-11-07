/* What src/main/filesystem.c uses besides the CD, for cdtest. No game
 * headers: the stubs take any arguments. */

#include <stdlib.h>
#include <stdint.h>

int cdReadSetlocCount, cdReadCommandOkCount, cdReadCommandErrorCount, cdReadRecoveryCount, cdReadRecoveryErrorCount, D_8005A49C, cdStopRecoveryCount, cdStopRecoveryOkCount, cdStopRecoveryErrorCount,
    cdErrorCount;
short streamFrameNumber;
_Alignas(16) unsigned char portSpuRegisters[0x200];

void* allocateHeap(int size, int flags) {
    (void)flags;
    return calloc(1, (size_t)size + 0x800);
}

void freeHeap(void* p) {
    free(p);
}

#define STUB(name) \
    int name() { return 0; }
STUB(resetMemoryAllocStats)
STUB(ClearImage)
STUB(ClearOTagR)
STUB(DrawOTag)
STUB(DrawSync)
STUB(PCinit)
STUB(initDebugFont)
STUB(InitGeom)
STUB(logToScreen)
STUB(MoveImage)
STUB(PCclose)
STUB(PClseek)
STUB(PCopen)
STUB(PCread)
STUB(printDebugTextOnScreen)
STUB(PutDispEnv)
STUB(PutDrawEnv)
STUB(ResetGraph)
STUB(SetDefDispEnv)
STUB(SetDefDrawEnv)
STUB(SetDispMask)
STUB(sprintf_screen)

int loadImageCalls;

int LoadImage(void* rect, void* data) {
    (void)rect;
    (void)data;
    loadImageCalls++;
    return 0;
}

/* port/src/system/spu.c: decoded XA-ADPCM audio (port/src/cd/xa.c). */
void spuCdSubmit(const short* lr, int frames, int rate) {
    (void)lr;
    (void)frames;
    (void)rate;
}

/* The standalone loader test has no settings, shutdown UI or slowdown
 * recording. Keep those unrelated services outside the CD integration. */
const char* settingsGet(const char* key) { (void)key; return NULL; }
int settingsInt(const char* key, int def) { (void)key; return def; }
int settingsBool(const char* key, int def) { (void)key; return def; }
void settingsDescribe(const char* key, const char* text) { (void)key; (void)text; }
void settingsSet(const char* key, const char* value) { (void)key; (void)value; }
void settingsSetInt(const char* key, int value) { (void)key; (void)value; }
int settingsSave(void) { return 1; }
unsigned settingsRevision(void) { return 0; }
void quitNow(void) { exit(1); }
double slowdownCdCommand(int command) { (void)command; return -1; }
void slowdownCdRead(int command, int32_t lba) { (void)command; (void)lba; }
double slowdownCdHold(double at, int32_t lba) { (void)lba; return at; }
double slowdownCdFirst(void) { return -1; }
void slowdownCdDelivered(void) {}
void slowdownCdComplete(int status) { (void)status; }
