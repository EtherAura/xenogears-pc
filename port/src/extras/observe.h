#ifndef XG_OBSERVE_H
#define XG_OBSERVE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XG_TRACE_CAPACITY 4096
#define XG_HISTORY_CAPACITY 64
#define XG_HISTORY_WIDTH 512
#define XG_HISTORY_LINES 128
typedef struct { uint64_t sequence, vblank; int field, actor, slot, routine, pc, op, ext; } XgTrace;
typedef struct { uint64_t sequence; int field, actor, dialog, lines, width, truncated; uint16_t body, outline; const uint8_t* pixels; } XgHistory;
uint64_t xgObserveVblank(void);
void xgTraceEnable(int on);
int xgTraceEnabled(void);
void xgTraceClear(void);
int xgTraceCount(void);
int xgTraceGet(int index, XgTrace* out);
void xgHistoryEnable(int on);
int xgHistoryEnabled(void);
void xgHistoryClear(void);
int xgHistoryCount(void);
int xgHistoryGet(int index, XgHistory* out);
#ifdef __cplusplus
}
#endif
#endif
