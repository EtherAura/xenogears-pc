/* Bounded native observers. Storage is outside every game/sandbox module.
 * History copies only already printed pixels, including substitutions and
 * two-byte JP glyphs. It never decodes ahead or calls the text printer. */
#include "observe.h"
#include "sandbox.h"
#include "system/hw.h"
#include "main/dialog.h"
#include <stdlib.h>
#include <string.h>

typedef struct { XgHistory info; uint8_t* pixels; } History;
typedef struct { const void* window; int field, actor, dialog, x, line, row; uint64_t entry; } Window;
static Window windows[4];
static History history[XG_HISTORY_CAPACITY];
static XgTrace trace[XG_TRACE_CAPACITY];
static uint64_t traceSerial, historySerial;
static int traceCount, historyCount, traceOn, historyOn = 1, configured;
static void configure(void) {
    if (configured) return;
    configured = 1;
    const char* t = getenv("XG_SCRIPT_TRACE");
    const char* h = getenv("XG_DIALOG_HISTORY");
    if (t) traceOn = atoi(t) != 0;
    if (h) historyOn = atoi(h) != 0;
}
uint64_t xgObserveVblank(void) { return hwVSyncCount(); }
void xgTraceEnable(int on) { configure(); traceOn = on != 0; }
int xgTraceEnabled(void) { configure(); return traceOn; }
void xgTraceClear(void) { traceCount = 0; }
int xgTraceCount(void) { return traceCount; }
int xgTraceGet(int index, XgTrace* out) {
    if (!out || index < 0 || index >= traceCount) return 0;
    *out = trace[(traceSerial - (uint64_t)traceCount + (uint64_t)index) % XG_TRACE_CAPACITY];
    return 1;
}
void pcObserveOp(int field, int actor, int slot, int routine, int pc, int op, int ext) {
    configure();
    if (!traceOn || xgSandboxCurrent()) return;
    XgTrace* t = &trace[traceSerial % XG_TRACE_CAPACITY];
    *t = (XgTrace){++traceSerial, hwVSyncCount(), field, actor, slot, routine, pc, op, ext};
    if (traceCount < XG_TRACE_CAPACITY) traceCount++;
}
void xgHistoryEnable(int on) { configure(); historyOn = on != 0; }
int xgHistoryEnabled(void) { configure(); return historyOn; }
void xgHistoryClear(void) {
    for (int i = 0; i < XG_HISTORY_CAPACITY; i++) { free(history[i].pixels); memset(&history[i], 0, sizeof(history[i])); }
    historyCount = 0;
    for (int i = 0; i < 4; i++) windows[i].entry = 0;
}
int xgHistoryCount(void) { return historyCount; }
int xgHistoryGet(int index, XgHistory* out) {
    if (!out || index < 0 || index >= historyCount) return 0;
    *out = history[(historySerial - (uint64_t)historyCount + (uint64_t)index) % XG_HISTORY_CAPACITY].info;
    return 1;
}
static Window* find(const void* window) {
    for (int i = 0; i < 4; i++) if (windows[i].window == window) return &windows[i];
    return NULL;
}
void pcObserveWindow(const void* window, int field, int actor, int dialog) {
    configure();
    if (xgSandboxCurrent()) return;
    Window* w = find(window);
    if (!w) { for (int i = 0; i < 4; i++) if (!windows[i].window) { w = &windows[i]; break; } }
    if (w) *w = (Window){window, field, actor, dialog, -1, -1, -1, 0};
}
void pcObserveText(const void* window) {
    if (xgSandboxCurrent()) return;
    Window* w = find(window);
    if (!w) return;
    w->entry = 0; w->row = w->line = w->x = -1;
}
void pcObserveLine(const void* window, const void* image, int stride, int x, int width, int line, int odd) {
    configure();
    if (!historyOn || xgSandboxCurrent() || !image || stride <= 0 || stride > 128 || x < 0 || width <= 0 || x >= stride || width >= stride - x) return;
    Window* w = find(window);
    if (!w) return;
    if (!w->entry) {
        History* h = &history[historySerial % XG_HISTORY_CAPACITY];
        uint8_t* pixels = calloc(XG_HISTORY_WIDTH * 14 * XG_HISTORY_LINES, 1);
        if (!pixels) return;
        free(h->pixels);
        memset(h, 0, sizeof(*h));
        h->pixels = pixels;
        h->info = (XgHistory){++historySerial, w->field, w->actor, w->dialog, 0, 0, 0, fontPalettes[1], fontPalettes[2], h->pixels};
        w->entry = historySerial; w->row = w->x = w->line = -1;
        if (historyCount < XG_HISTORY_CAPACITY) historyCount++;
    }
    History* h = &history[(w->entry - 1) % XG_HISTORY_CAPACITY];
    if (h->info.sequence != w->entry) { w->entry = 0; return; }
    if (line != w->line || x <= w->x) w->row++;
    w->line = line; w->x = x;
    if (w->row >= XG_HISTORY_LINES) { h->info.truncated = 1; return; }
    h->info.lines = w->row + 1;
    const uint16_t* words = image;
    /* The sprite displays exactly cursorX * 4 pixels. The printer may
     * leave outline bits beyond that width; copying them exposes stale
     * pixels from the shared RAM line (especially after JP wide glyphs). */
    int end = (x + width) * 4;
    if (end > stride * 4) end = stride * 4;
    if (end > h->info.width) h->info.width = end;
    for (int y = 0; y < 13; y++) for (int p = 0; p < end; p++) {
        unsigned value = words[y * stride + p / 4] >> ((p & 3) * 4);
        h->pixels[(w->row * 14 + y) * XG_HISTORY_WIDTH + p] = (uint8_t)((value >> (odd ? 2 : 0)) & 3);
    }
}
