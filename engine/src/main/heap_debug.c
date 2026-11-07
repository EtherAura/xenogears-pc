/*
 * Heap debugging: dump the MCB list to a file on the PC host.
 *
 * File 0x80032DCC-0x80032E88 (main executable): it reaches debugPrintSink via
 * %hi/%lo while heap.c defines it, and it defines heapDumpFileHandle ($gp).
 * Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "psyq.h"
#include "main/debug_font.h"

static s32 heapDumpFileHandle;                     /* PC-host file handle */
extern void (*debugPrintSink)(const char*, ...);   /* debug print sink (heap file) */
void PCinit(void);
s32 PCclose(s32);
void dumpHeapBlocks(s32, s32, s32, s32);

/* Debug sink: write a string to the dump file. */
void writeHeapDumpString(char* str) {
    PCwrite(heapDumpFileHandle, str, (u32)strlen(str));
}

/* Dump the heap block list to a PC-host file. */
void dumpHeapToPcFile(char* name) {
    PCinit();
    heapDumpFileHandle = PCcreat(name, 0);
    debugPrintSink = (void (*)(const char*, ...))writeHeapDumpString;
    dumpHeapBlocks(1, 0, 0, -1);
    debugPrintSink = sprintf_screen;
    PCclose(heapDumpFileHandle);
}

void* func_80032E7C(void** p) {
    return *p;
}
