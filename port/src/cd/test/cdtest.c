/*
 * CD layer test without the rest of the port: the game's own loader
 * (src/main/filesystem.c) on top of port/src/cd, with the hardware the loader
 * touches beyond the CD stubbed out. Runs the boot's loads (kernelMain:
 * initCDAndFileSystem, files 2-7), a batched load of the same files and a
 * seek, and writes what it read for port/src/cd/test/cdcheck.py.
 *
 *   port/src/cd/test/build.sh && build/pc/cdtest/cdtest [disc.cue] [outdir]
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd/cd.h"
#include "common.h"
#include "main/filesystem.h"
#include "psyq.h"
#include "system/hw.h"

s32 VSync(s32 mode) {
    hwSafePoint();
    return 0;
}

extern s32 readFileErrorCount, batchReadErrorCount, streamReadErrorCount;
extern s32 cdReadCommandErrorCount, cdReadRecoveryCount, cdReadRecoveryErrorCount, cdStopRecoveryCount;
extern s32 readInProgress;
extern s32 loadImageCalls;
void* allocateHeap(s32 size, s32 flags);
s32* allocateBufferForVramUpload(s32 count, s32 flags);
u8* getNextStreamedSector(void);
s32 releaseSectorSlot(u8* sector);
s32 loadImageFileToVram(s32 fileIndex, s32* buf, s32 channel, s32 flags, u16 arg4, u16 arg5, u16 arg6, u16 arg7,
                        u16 arg8, u16 arg9);
void StSetRing(u32* ring_addr, u32 ring_size);
void StUnSetRing(void);
void StSetStream(u32 mode, u32 start_frame, u32 end_frame, void (*func1)(), void (*func2)());
u32 StGetNext(u32** addr, u32** header);
u32 StFreeRing(u32* base);
s32 CdRead2(s32 mode);

/* A file's bytes straight from the image (Mode 2 Form 1 user data). */
static u8* discFile(int sector, int size) {
    u8* buf = malloc((size_t)size + 0x800);
    u8 raw[CD_RAW_SECTOR];
    for (int i = 0; i * 0x800 < size; i++) {
        cdReadRawSector(sector + i, raw);
        memcpy(buf + i * 0x800, raw + 24, 0x800);
    }
    return buf;
}

/* The file index of global table entry `entry`, relative to the current
 * directory (the loader adds the directory's base). */
static int fileIndexOf(int entry) {
    s32 dir, off;
    return entry + 1 - getCurrentDirectory(&dir, &off);
}

static double now(void) {
    return hwNow();
}

static void dump(const char* dir, const char* name, const void* data, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* fp = fopen(path, "wb");
    if (fp == NULL || fwrite(data, 1, size, fp) != size) {
        fprintf(stderr, "cdtest: cannot write %s\n", path);
        exit(1);
    }
    fclose(fp);
}

int main(int argc, char** argv) {
    const char* disc = argc > 1 ? argv[1] : XG_ROOT "/bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue";
    const char* out = argc > 2 ? argv[2] : XG_ROOT "/build/pc/cdtest/out";
    if (!cdOpenDisc(disc)) {
        return 1;
    }
    static u8 fileTable[0x8000 + 0x800];
    static u16 dirTable[0x400];

    double t0 = now();
    initCDAndFileSystem(fileTable, dirTable, 0);
    double t1 = now();
    printf("initCDAndFileSystem: %.3f s\n", t1 - t0);
    dump(out, "filetable.bin", fileTable, 0x8000);
    dump(out, "dirtable.bin", dirTable, 0x7A);

    /* kernelMain's loads. */
    setCurrentDirectory(0, 1);
    void* files[8] = { 0 };
    s32 sizes[8] = { 0 };
    t0 = now();
    for (int i = 2; i <= 5; i++) {
        sizes[i] = getFileSize(i);
        files[i] = allocateHeap(sizes[i], 0);
    }
    for (int i = 2; i <= 5; i++) {
        readFile(i, files[i], 0, 0);
    }
    waitReadCompletion(0);
    for (int i = 6; i <= 7; i++) {
        sizes[i] = getFileSize(i);
        files[i] = allocateHeap(sizes[i], 0);
        readFile(i, files[i], 0, 0);
        waitReadCompletion(0);
    }
    t1 = now();
    s32 total = 0;
    for (int i = 2; i <= 7; i++) {
        char name[32];
        snprintf(name, sizeof name, "file%d.bin", i);
        dump(out, name, files[i], (size_t)sizes[i]);
        total += sizes[i];
        printf("file %d: sector %d, %d bytes\n", i, getFileStartSector(i), sizes[i]);
    }
    printf("files 2-7: %d bytes in %.3f s (%.0f KB/s)\n", total, t1 - t0, total / 1024.0 / (t1 - t0));

    /* The same files as one batch (sorted by the loader, gaps skipped). */
    LoadingBatchCommand batch[7];
    void* again[8] = { 0 };
    int n = 0;
    for (int i = 7; i >= 2; i--) {
        again[i] = allocateHeap(sizes[i], 0);
        batch[n].m0_fileIndex = (u16)i;
        batch[n].m4_loadPtr = again[i];
        n++;
    }
    batch[n].m0_fileIndex = 0;
    batch[n].m4_loadPtr = NULL;
    t0 = now();
    batchStartLoadingFiles(batch, 0, 0);
    waitReadCompletion(0);
    t1 = now();
    int same = 1;
    for (int i = 2; i <= 7; i++) {
        if (memcmp(files[i], again[i], (size_t)sizes[i]) != 0) {
            printf("batch: file %d differs\n", i);
            same = 0;
        }
    }
    printf("batch of files 2-7: %s in %.3f s\n", same ? "same bytes" : "DIFFERENT", t1 - t0);

    /* Streamed read into a sector buffer (readFile flag 0x100): the ready
     * callback fills slots, the data callback completes them in order. A
     * slow reader fills the buffer, and the loader's recovery (Pause, seek
     * back, read again) has to keep the data right. */
    for (int slow = 0; slow < 2; slow++) {
        int f = 3;
        s32* ringBuf = allocateBufferForVramUpload(8, 0);
        u8* got = malloc((size_t)sizes[f] + 0x800);
        int n = 0;
        s32 recoveries = cdReadRecoveryCount;
        t0 = now();
        readFile(f, ringBuf, 0, 0x100);
        double next = t0;
        while (n * 0x800 < sizes[f] && now() - t0 < 30) {
            u8* p = now() >= next ? getNextStreamedSector() : NULL;
            if (p == NULL) {
                portYield();
                continue;
            }
            memcpy(got + n * 0x800, p, 0x800);
            releaseSectorSlot(p);
            n++;
            next = now() + (slow ? 0.03 : 0);
        }
        waitReadCompletion(0);
        t1 = now();
        int ok = memcmp(got, files[f], (size_t)sizes[f]) == 0;
        printf("stream read of file %d%s: %d sectors, %s in %.3f s, %d recoveries\n", f, slow ? " (slow reader)" : "",
               n, ok ? "same bytes" : "DIFFERENT", t1 - t0, cdReadRecoveryCount - recoveries);
        same &= ok;
    }

    /* Image file streamed to VRAM (loadImageFileToVram): LoadImage per slice. */
    {
        int f = fileIndexOf(609);
        s32* ringBuf = allocateBufferForVramUpload(16, 0);
        t0 = now();
        int r = loadImageFileToVram(f, ringBuf, 0, 0, 0, 0, 0, 0, 0, 0);
        waitReadCompletion(0);
        t1 = now();
        printf("loadImageFileToVram(entry 609, %d bytes): %d, %d LoadImage calls, in %.3f s\n", getFileSize(f), r,
               loadImageCalls, t1 - t0);
        same &= r == 0 && loadImageCalls > 0;
    }

    /* STR streaming through the St* ring (as video_player): ReadS of a movie
     * with XA audio filtered out; each frame checked against the disc. */
    {
        int entry = 1;
        int f = fileIndexOf(entry);
        int lba = getFileStartSector(f);
        static u32 ring[32 * 512];
        StSetRing(ring, 32);
        StSetStream(0, 1, 0xFFFFFFFF, NULL, NULL);
        u8 param[4] = { 1, 1, 0, 0 };
        CdControlB(0x0D, param, NULL);
        CdlLOC pos;
        CdIntToPos(lba, &pos);
        CdControlB(0x02, (u8*)&pos, NULL);
        CdRead2(0x1C8);
        int frames = 0, bad = 0, scan = lba;
        u32 lastFrame = 0;
        t0 = now();
        while (frames < 30 && now() - t0 < 20) {
            u32* addr;
            u32* hdr;
            if (StGetNext(&addr, &hdr) != 0) {
                portYield();
                continue;
            }
            u16 nSectors = ((u16*)hdr)[3];
            u32 frame = hdr[2];
            /* The frame's video sectors on the disc, in order. */
            u8* want = malloc((size_t)nSectors * 2016);
            int found = 0;
            u8 raw[CD_RAW_SECTOR];
            while (found < nSectors && scan < lba + 20000) {
                cdReadRawSector(scan++, raw);
                const u8* h = raw + 24;
                if ((raw[18] & 0x44) == 0x44 || h[0] != 0x60 || h[1] != 0x01) {
                    continue;
                }
                u32 fn;
                memcpy(&fn, h + 8, 4);
                if (fn != frame) {
                    continue;
                }
                memcpy(want + found * 2016, h + 32, 2016);
                found++;
            }
            if (found != nSectors || memcmp(want, addr, (size_t)nSectors * 2016) != 0 ||
                (lastFrame != 0 && frame != lastFrame + 1)) {
                bad++;
            }
            free(want);
            lastFrame = frame;
            StFreeRing(addr);
            frames++;
        }
        t1 = now();
        StUnSetRing();
        CdControlB(0x09, NULL, NULL);
        printf("STR ring: %d frames up to %u, %d bad, in %.3f s (%.1f fps)\n", frames, lastFrame, bad, t1 - t0,
               frames / (t1 - t0));
        same &= frames == 30 && bad == 0;
    }

    /* A blocking command sequence like the disc-check code's. */
    u8 result[8];
    CdlLOC loc;
    t0 = now();
    int ok = CdControlB(0x01, NULL, result);
    printf("CdControlB(Nop) = %d stat %02X\n", ok, result[0]);
    ok = CdControlB(0x13, NULL, result);
    printf("CdControlB(GetTN) = %d: %02X %02X %02X\n", ok, result[0], result[1], result[2]);
    CdIntToPos(200000, &loc);
    ok = CdControlB(0x02, (u8*)&loc, result);
    ok = ok && CdControlB(0x15, NULL, result);
    t1 = now();
    printf("Setloc+SeekL to 200000: %d in %.3f s\n", ok, t1 - t0);
    ok = CdControlB(0x09, NULL, result);
    printf("Pause: %d stat %02X\n", ok, result[0]);

    /* Disc change: the shell opens and closes on the other disc; the game
     * polls Nop until the shell bit clears, then starts over. */
    if (argc > 3) {
        cdChangeDisc(argv[3]);
        int polls = 0;
        t0 = now();
        do {
            polls++;
            CdControlB(0x01, NULL, result);
        } while ((result[0] & 0x10) && now() - t0 < 10);
        initCDAndFileSystem(fileTable, dirTable, 0);
        setCurrentDirectory(0, 1);
        s32 size = getFileSize(2);
        u8* buf = allocateHeap(size, 0);
        readFile(2, buf, 0, 0);
        waitReadCompletion(0);
        u8* want = discFile(getFileStartSector(2), size);
        int ok = memcmp(buf, want, (size_t)size) == 0;
        printf("disc change: %d Nops until the shell closed, file 2 of the new disc %s (%.3f s)\n", polls,
               ok ? "matches" : "DIFFERS", now() - t0);
        same &= ok;
    }

    printf("errors: read %d batch %d stream %d; recoveries %d %d %d %d\n", readFileErrorCount, batchReadErrorCount,
           streamReadErrorCount, cdReadCommandErrorCount, cdReadRecoveryCount, cdReadRecoveryErrorCount, cdStopRecoveryCount);
    return readFileErrorCount + batchReadErrorCount != 0 || !same;
}
