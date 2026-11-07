/*
 * LIBCARD and the BIOS memory card services: the card driver that runs on
 * the vblank interrupt, _card_info and its events, and the "bu" file device
 * (B(32h)..B(45h)) on the card images of card.h.
 *
 * Timing (psx-spx "Memory Card Functions"; measured on the real BIOS in
 * Beetle PSX 0.9.44 as a black box: a probe calling the BIOS from the game's
 * main thread, work/agents/NPFIX/cdcard/cardprobe): the BIOS serves one slot
 * per vblank, alternating; after StartCARD the next vblank serves slot 2. A
 * request waits for its slot's vblank. A sector access then completes 0.415
 * of a frame after that vblank (firstfile, open, erase, file reads: 0.41 to
 * 0.42), _card_info 0.024; a request to an empty slot times out just after
 * the following vblank, which stays with that slot. The BIOS file functions
 * are made of such accesses (one per 128-byte sector, one per directory
 * frame written, status checks around writes) and wait for each, so a
 * synchronous one-block load takes some 140 vblanks, as on the console.
 *
 * Not modelled: on the PS1 the BIOS's pad/card handler now and then misses
 * a vblank while the CD delivers sectors (Beetle: 3 of the 11 vblanks of the
 * ReadN after the load of a save; its slot toggle in kernel RAM stops), which
 * swaps the slots' vblanks from there; when depends on the PS1's CPU timing,
 * which the port does not have.
 *
 * Two PC choices: the kernel's power-on card scan (the first _bu_init)
 * completes at once, as the PC skips the console's other power-on time; and
 * XG_CARD_FAST=1 makes every access immediate. XG_CARD_LOG=1 logs the BIOS
 * calls with their vblank, 2 also every access.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "system/system.h"

#include "card.h"
#include "../system/invariants.h"
#include "../system/savestate.h"

/* Weak: the card unit tests (card_biostest, card_speeduptest) link bu.c
 * without the game and its invariants log. */
extern void invariantCardWrite(int slot, int sector, const uint8_t* frame) __attribute__((weak));
#include "../system/speedup.h"

/* ---- the card driver ---- */

enum { ACC_IDLE, ACC_QUEUED, ACC_BUSY, ACC_DONE };
enum { RES_OK, RES_TIMEOUT, RES_NEW, RES_ERROR };
enum { OP_INFO, OP_READ, OP_WRITE };

/* When an access completes, in frames after its slot's vblank (Beetle PSX,
 * the top of the file; the refused access is not measured). */
#define PHASE_SECTOR 0.415  /* a 128-byte sector transfer */
#define PHASE_INFO 0.024    /* the status bytes only */
#define PHASE_REFUSED 0.035 /* a read or write refused at the FLAG byte (new card) */
#define PHASE_TIMEOUT 0.007 /* after the vblank that follows the unanswered one */

typedef struct {
    int state;
    int op;
    int sector;
    int ignoreNew; /* _new_card: do not refuse on the new-card flag */
    int event;     /* _card_info: the answer is a SwCARD event, nobody waits */
    int result;
    uint8_t frame[CARD_FRAME]; /* read destination / write source */
} Access;

static Access accesses[2];
static int started;    /* between StartCARD and StopCARD */
static int turn;       /* the slot the next vblank serves */
static int timeoutSlot = -1;
static int fast;       /* XG_CARD_FAST */
static int logLevel;   /* XG_CARD_LOG: 1 BIOS calls, 2 also each access */
static int instant;    /* accesses complete at once (fast, or the power-on scan) */

static void cardLog(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void cardLog(int level, const char* fmt, ...) {
    if (logLevel < level) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "card %u: ", (unsigned)hwVSyncCount());
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void complete(void* arg) {
    int slot = (int)(uptr)arg;
    Access* a = &accesses[slot];
    Card* c = &cards[slot];

    cardLog(2, "  slot %d %s %d -> %d", slot + 1, a->op == OP_INFO ? "info" : a->op == OP_READ ? "read" : "write",
         a->sector, a->result);

    if (a->result == RES_OK && a->op == OP_READ) {
        memcpy(a->frame, c->data + a->sector * CARD_FRAME, CARD_FRAME);
    } else if (a->result == RES_OK && a->op == OP_WRITE) {
        uint8_t* dst = c->data + a->sector * CARD_FRAME;
        if (memcmp(dst, a->frame, CARD_FRAME) != 0) {
            memcpy(dst, a->frame, CARD_FRAME);
            c->dirty = 1;
        }
        c->flagNew = 0;
    }
    if (a->event) {
        a->state = ACC_IDLE;
        DeliverEvent(EV_CLASS_SWCARD, a->result == RES_TIMEOUT ? EV_SPEC_TIMEOUT
                                      : a->result == RES_NEW   ? EV_SPEC_NEW
                                      : a->result == RES_ERROR ? EV_SPEC_ERROR
                                                               : EV_SPEC_IOE);
        return;
    }
    a->state = ACC_DONE;
}

/* What the card answers, and when (frames after now). */
static double execute(int slot) {
    Access* a = &accesses[slot];
    Card* c = &cards[slot];

    if (a->op == OP_INFO) {
        a->result = c->flagNew ? RES_NEW : RES_OK;
        return PHASE_INFO;
    }
    if (a->sector < 0 || a->sector >= CARD_FRAMES) {
        a->result = RES_ERROR;
        return PHASE_REFUSED;
    }
    if (c->flagNew && !a->ignoreNew) {
        a->result = RES_NEW;
        return PHASE_REFUSED;
    }
    a->result = RES_OK;
    return PHASE_SECTOR;
}

/* The vblank interrupt's card part (port/src/system/hw.c), after the pads. */
void cardVBlank(void) {
    if (!started || instant || slowdownBiosMissed()) {
        return; /* a replay: the PS1's BIOS handler skipped this vblank, the turn stays */
    }
    double frame = hwVSyncPeriod();
    if (timeoutSlot >= 0) {
        int slot = timeoutSlot;
        timeoutSlot = -1;
        accesses[slot].result = RES_TIMEOUT;
        hwScheduleAt(hwNow() + PHASE_TIMEOUT * frame, complete, (void*)(uptr)slot);
        return; /* this vblank stays with the slot that timed out */
    }
    int slot = turn;
    turn ^= 1;
    Access* a = &accesses[slot];
    if (a->state != ACC_QUEUED) {
        return;
    }
    a->state = ACC_BUSY;
    if (!cards[slot].present) {
        timeoutSlot = slot;
        return;
    }
    hwScheduleAt(hwNow() + execute(slot) * frame, complete, (void*)(uptr)slot);
}

static void waitIdle(int slot) {
    Access* a = &accesses[slot];
    while (a->state == ACC_QUEUED || a->state == ACC_BUSY) {
        hwYield(); /* a _card_info still running */
    }
}

static void startAccess(int slot, int op, int sector, int ignoreNew, int event) {
    Access* a = &accesses[slot];
    waitIdle(slot);
    a->op = op;
    a->sector = sector;
    a->ignoreNew = ignoreNew;
    a->event = event;
    a->state = ACC_QUEUED;
    /* Apply the player's choice only to this new access. Do not stop the
     * vblank driver or rewrite completion events: a queued/timed access
     * must still finish if Speedup changes while its caller is waiting. */
    if (instant || speedupEnabled() || !started) {
        a->state = ACC_BUSY;
        if (!cards[slot].present) {
            a->result = RES_TIMEOUT;
        } else {
            execute(slot);
        }
        complete((void*)(uptr)slot);
    }
}

/* One synchronous sector access; the result (RES_*). */
static int cardAccess(int slot, int op, int sector, uint8_t* frame, int ignoreNew) {
    Access* a = &accesses[slot];
    waitIdle(slot);
    if (op == OP_WRITE) {
        memcpy(a->frame, frame, CARD_FRAME);
        if (invariantCardWrite) invariantCardWrite(slot, sector, frame); /* XG_INVARIANT_LOG */
    }
    startAccess(slot, op, sector, ignoreNew, 0);
    while (a->state != ACC_DONE) {
        hwYield();
    }
    a->state = ACC_IDLE;
    if (op == OP_READ && a->result == RES_OK && frame != NULL) {
        memcpy(frame, a->frame, CARD_FRAME);
    }
    return a->result;
}

/* ---- the BIOS's view of each card ---- */

/* The BIOS reads the header, acknowledges the new-card flag with a write of
 * the header to the write test frame, then reads the directory and the broken
 * sector list. Returns whether the card is formatted. */
static int loadDirectory(int slot) {
    uint8_t frame[CARD_FRAME];
    uint8_t header[CARD_FRAME];

    if (cardAccess(slot, OP_READ, 0, header, 1) != RES_OK || !cardIsFormatted(header)) {
        return 0;
    }
    if (cardAccess(slot, OP_WRITE, 0x3F, header, 1) != RES_OK) {
        return 0;
    }
    for (int i = 1; i < 36; i++) {
        if (cardAccess(slot, OP_READ, i, frame, 1) != RES_OK) {
            return 0;
        }
    }
    return 1;
}

/* What open, firstfile and erase do first: read the header, which also tells
 * whether the card was changed (then the directory is read again). */
static int checkCard(int slot) {
    uint8_t header[CARD_FRAME];
    int r = cardAccess(slot, OP_READ, 0, header, 0);

    if (r == RES_NEW) {
        if (cardAccess(slot, OP_READ, 0, header, 1) != RES_OK || !cardIsFormatted(header)) {
            return 0;
        }
        return loadDirectory(slot);
    }
    return r == RES_OK && cardIsFormatted(header);
}

/* The status check the BIOS makes around writes: a write's errors show in
 * the next command's status. */
static int checkStatus(int slot) {
    return cardAccess(slot, OP_INFO, 0, NULL, 0) == RES_OK;
}

/* Write directory frames from an edited copy of block 0, one access each,
 * between two status checks (as the BIOS's create, rename and erase do). */
static int writeFrames(int slot, const uint8_t* dir, const int* frames, int n) {
    if (!checkStatus(slot)) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        uint8_t frame[CARD_FRAME];
        memcpy(frame, dir + frames[i] * CARD_FRAME, CARD_FRAME);
        if (cardAccess(slot, OP_WRITE, frames[i], frame, 0) != RES_OK) {
            return 0;
        }
    }
    return checkStatus(slot);
}

/* "buXY:name" -> slot X (Y, the multitap port, must be 0) and the name. */
static int parseDevice(const char* path, const char** name) {
    if (path == NULL || strncmp(path, "bu", 2) != 0 || path[2] < '0' || path[2] > '1' || path[3] != '0' ||
        path[4] != ':') {
        return -1;
    }
    *name = path + 5;
    return path[2] - '0';
}

/* ---- LIBCARD ---- */

void InitCARD(s32 padEnable) {
    hwSafePoint();
    cardImagesInit();
    fast = getenv("XG_CARD_FAST") != NULL && atoi(getenv("XG_CARD_FAST")) != 0;
    logLevel = getenv("XG_CARD_LOG") != NULL ? atoi(getenv("XG_CARD_LOG")) : 0;
    cardLog(1, "InitCARD");
    started = 0;
    timeoutSlot = -1;
    for (int i = 0; i < 2; i++) {
        accesses[i].state = ACC_IDLE;
    }
}

void StartCARD(void) {
    hwSafePoint();
    cardLog(1, "StartCARD");
    started = 1;
    turn = 1;
    timeoutSlot = -1;
}

void StopCARD(void) {
    hwSafePoint();
    started = 0;
}

/* ---- BIOS files: the "bu" device (and the console, fd 1) ---- */

#define MAX_FILES 16

typedef struct {
    int used;
    int slot;
    int blocks[CARD_BLOCKS];
    int nblocks;
    int size;
    int pos;
} File;

static File files[MAX_FILES]; /* 0 and 1 are the console's */

static struct {
    int slot;
    int next; /* the block to look at next */
    char pattern[CARD_NAME_MAX + 1];
} search = {0, CARD_BLOCKS, ""};

static void closeAll(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].used = 0;
    }
}

int cardBusy(int slot) {
    if (accesses[slot].state != ACC_IDLE) {
        return 1;
    }
    for (int i = 2; i < MAX_FILES; i++) {
        if (files[i].used && files[i].slot == slot) {
            return 1;
        }
    }
    return 0;
}

static int powerOnScanDone; /* the first _bu_init is the power-on scan (instant) */

/* A(70h) _bu_init: the directories of both cards into the BIOS. */
void _bu_init(void) {
    hwSafePoint();
    cardLog(1, "_bu_init");
    closeAll();
    instant = fast || !powerOnScanDone;
    loadDirectory(0);
    loadDirectory(1);
    instant = fast;
    powerOnScanDone = 1;
    cardFlush(0);
    cardFlush(1);
    cardLog(1, "_bu_init done");
}

/* A(ABh) _card_info: the result arrives as a SwCARD event. */
s32 _card_info(s32 chan) {
    hwSafePoint();
    int slot = (chan >> 4) & 1;
    if ((chan & 0xF) != 0) {
        DeliverEvent(EV_CLASS_SWCARD, EV_SPEC_TIMEOUT); /* no multitap */
        return 1;
    }
    cardLog(1, "_card_info %02x", chan);
    startAccess(slot, OP_INFO, 0, 0, 1);
    return 1;
}

static void fillDirEntry(uint8_t* dir, int slot, int block) {
    const uint8_t* f = cards[slot].data + block * CARD_FRAME;
    char name[CARD_NAME_MAX + 1];
    memcpy(name, f + 0x0A, CARD_NAME_MAX);
    name[CARD_NAME_MAX] = '\0';
    uint32_t words[4] = {f[0] & 0xF0u, (uint32_t)(f[4] | f[5] << 8 | f[6] << 16 | f[7] << 24), (uint32_t)block,
                         (uint32_t)block * 64};
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 4; k++) {
            dir[0x14 + i * 4 + k] = (uint8_t)(words[i] >> (8 * k));
        }
    }
    /* Then the name with its 00h; the rest of the name field keeps what was
     * there. A 20-character name (JP saves: BISLPS-01160 + 8 digits) ends
     * with its 00h on the attribute's low byte, as the BIOS leaves it (Redux:
     * the JP menu reads the name back terminated after 20 characters). */
    memcpy(dir, name, strlen(name) + 1);
}

static void* searchNext(void* dir) {
    while (search.next < CARD_BLOCKS) {
        int b = search.next++;
        const uint8_t* f = cards[search.slot].data + b * CARD_FRAME;
        char name[CARD_NAME_MAX + 1];
        if (f[0] != DIR_FIRST) {
            continue;
        }
        memcpy(name, f + 0x0A, CARD_NAME_MAX);
        name[CARD_NAME_MAX] = '\0';
        if (cardNameMatch(search.pattern, name)) {
            fillDirEntry(dir, search.slot, b);
            return dir;
        }
    }
    return NULL;
}

/* B(42h) firstfile */
static void* firstfileImpl(char* path, void* dir) {
    const char* name;
    hwSafePoint();
    int slot = parseDevice(path, &name);
    if (slot < 0 || !checkCard(slot)) {
        search.next = CARD_BLOCKS;
        return NULL;
    }
    search.slot = slot;
    search.next = 1;
    snprintf(search.pattern, sizeof(search.pattern), "%s", name);
    return searchNext(dir);
}

/* B(43h) nextfile */
static void* nextfileImpl(void* dir) {
    hwSafePoint();
    return searchNext(dir);
}

/* B(32h) open: bit 9 creates a file of mode >> 16 blocks. */
static s32 openImpl(char* path, s32 mode) {
    const char* name;
    hwSafePoint();
    int slot = parseDevice(path, &name);
    if (slot < 0 || name[0] == '\0' || !checkCard(slot)) {
        return -1;
    }
    int fd = 2;
    while (fd < MAX_FILES && files[fd].used) {
        fd++;
    }
    if (fd == MAX_FILES) {
        return -1;
    }
    Card* c = &cards[slot];
    int first = cardFindFile(c->data, name);
    if (mode & 0x200) {
        int nblocks = (u32)mode >> 16;
        int changed[CARD_BLOCKS];
        static uint8_t dir[CARD_BLOCK];
        if (first != 0 || strchr(name, '?') != NULL || strchr(name, '*') != NULL) {
            return -1;
        }
        memcpy(dir, c->data, CARD_BLOCK);
        first = cardCreateFile(dir, name, nblocks, changed);
        if (first == 0 || !writeFrames(slot, dir, changed, nblocks)) {
            return -1;
        }
        if (!cardFlush(slot)) {
            return -1;
        }
    } else if (first == 0) {
        return -1;
    }
    File* f = &files[fd];
    f->nblocks = cardFileBlocks(c->data, first, f->blocks, CARD_BLOCKS);
    if (f->nblocks < 0) {
        return -1;
    }
    const uint8_t* d = c->data + first * CARD_FRAME;
    f->size = d[4] | d[5] << 8 | d[6] << 16;
    if (f->size > f->nblocks * CARD_BLOCK) {
        f->size = f->nblocks * CARD_BLOCK;
    }
    f->slot = slot;
    f->pos = 0;
    f->used = 1;
    return fd;
}

static File* cardFile(s32 fd) {
    return fd >= 2 && fd < MAX_FILES && files[fd].used ? &files[fd] : NULL;
}

/* The card sector of the file's byte pos. */
static int fileSector(const File* f, int pos) {
    return f->blocks[pos / CARD_BLOCK] * 64 + (pos % CARD_BLOCK) / CARD_FRAME;
}

static s32 transfer(File* f, u8* buf, s32 n, int op) {
    if (n <= 0 || (n % CARD_FRAME) != 0 || (f->pos % CARD_FRAME) != 0 || f->pos + n > f->size) {
        return -1;
    }
    for (s32 done = 0; done < n; done += CARD_FRAME) {
        if (cardAccess(f->slot, op, fileSector(f, f->pos), buf + done, 0) != RES_OK) {
            return -1;
        }
        f->pos += CARD_FRAME;
    }
    if (op == OP_WRITE && !checkStatus(f->slot)) {
        return -1; /* the last sector's write failed */
    }
    return n;
}

/* B(34h) read */
static s32 readImpl(s32 fd, void* buf, s32 n) {
    hwSafePoint();
    File* f = cardFile(fd);
    return f != NULL ? transfer(f, buf, n, OP_READ) : -1;
}

/* B(35h) write: fd 1 is the BIOS console */
static s32 writeImpl(s32 fd, void* buf, s32 n) {
    hwSafePoint();
    if (fd == 1 && n > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
        return n;
    }
    File* f = cardFile(fd);
    if (f == NULL) {
        return -1;
    }
    s32 r = transfer(f, buf, n, OP_WRITE);
    return cardFlush(f->slot) ? r : -1;
}

/* B(36h) close */
static s32 closeImpl(s32 fd) {
    hwSafePoint();
    File* f = cardFile(fd);
    if (f == NULL) {
        return -1;
    }
    f->used = 0;
    return fd;
}

/* B(41h) format: the header, 15 free directory frames and the empty broken
 * sector list, written straight away (frames 0..35, as the BIOS does in
 * Redux: no status checks, the rest of the card untouched). */
static s32 formatImpl(char* path) {
    const char* name;
    static uint8_t blank[CARD_SIZE];

    hwSafePoint();
    int slot = parseDevice(path, &name);
    if (slot < 0 || !cards[slot].present) {
        return 0;
    }
    closeAll();
    cardFormatImage(blank);
    for (int i = 0; i < 36; i++) {
        if (cardAccess(slot, OP_WRITE, i, blank + i * CARD_FRAME, 1) != RES_OK) {
            cardFlush(slot);
            return 0;
        }
    }
    return cardFlush(slot);
}

/* B(44h) rename */
static s32 renameImpl(char* oldPath, char* newPath) {
    const char *oldName, *newName;
    static uint8_t dir[CARD_BLOCK];

    hwSafePoint();
    int slot = parseDevice(oldPath, &oldName);
    if (slot < 0 || parseDevice(newPath, &newName) != slot || newName[0] == '\0' || !checkCard(slot)) {
        return 0;
    }
    Card* c = &cards[slot];
    int first = cardFindFile(c->data, oldName);
    if (first == 0 || cardFindFile(c->data, newName) != 0) {
        return 0;
    }
    memcpy(dir, c->data, CARD_BLOCK);
    uint8_t* f = dir + first * CARD_FRAME;
    memset(f + 0x0A, 0, CARD_NAME_MAX + 1);
    strncpy((char*)f + 0x0A, newName, CARD_NAME_MAX);
    cardSetFrameChecksum(f);
    int ok = writeFrames(slot, dir, &first, 1);
    int saved = cardFlush(slot);
    return ok && saved;
}

/* B(45h) erase */
static s32 eraseImpl(char* path) {
    const char* name;
    static uint8_t dir[CARD_BLOCK];
    int changed[CARD_BLOCKS];

    hwSafePoint();
    int slot = parseDevice(path, &name);
    if (slot < 0 || !checkCard(slot)) {
        return 0;
    }
    Card* c = &cards[slot];
    int first = cardFindFile(c->data, name);
    if (first == 0) {
        return 0;
    }
    memcpy(dir, c->data, CARD_BLOCK);
    int n = cardDeleteFile(dir, first, changed);
    int ok = writeFrames(slot, dir, changed, n);
    int saved = cardFlush(slot);
    return ok && saved;
}

/* ---- the entry points, with XG_CARD_LOG ---- */

/* B(42h) firstfile */
void* func_80040584(char* path, void* dir) {
    void* r = firstfileImpl(path, dir);
    cardLog(1, "firstfile %s -> %s", path, r != NULL ? (char*)r : "none");
    return r;
}

/* B(43h) nextfile */
void* func_80040594(void* dir) {
    void* r = nextfileImpl(dir);
    cardLog(1, "nextfile -> %s", r != NULL ? (char*)r : "none");
    return r;
}

/* B(32h) open */
s32 func_80040534(char* path, s32 mode) {
    s32 r = openImpl(path, mode);
    cardLog(1, "open %s %x -> %d", path, (unsigned)mode, r);
    return r;
}

/* B(34h) read */
s32 func_80040544(s32 fd, void* buf, s32 n) {
    s32 r = readImpl(fd, buf, n);
    cardLog(1, "read %d %x -> %x", fd, (unsigned)n, (unsigned)r);
    return r;
}

/* B(35h) write */
s32 func_80040554(s32 fd, void* buf, s32 n) {
    s32 r = writeImpl(fd, buf, n);
    if (fd != 1) {
        cardLog(1, "write %d %x -> %x", fd, (unsigned)n, (unsigned)r);
    }
    return r;
}

/* B(36h) close */
s32 func_80040564(s32 fd) {
    s32 r = closeImpl(fd);
    cardLog(1, "close %d -> %d", fd, r);
    return r;
}

/* B(41h) format */
s32 func_80040574(char* path) {
    s32 r = formatImpl(path);
    cardLog(1, "format %s -> %d", path, r);
    return r;
}

/* B(44h) rename */
s32 func_800405A4(char* oldPath, char* newPath) {
    s32 r = renameImpl(oldPath, newPath);
    cardLog(1, "rename %s %s -> %d", oldPath, newPath, r);
    return r;
}

/* B(45h) erase */
s32 func_800405B4(char* path) {
    s32 r = eraseImpl(path);
    cardLog(1, "erase %s -> %d", path, r);
    return r;
}

/* ---- save states (system/savestate.h) ---- */

/* The driver between accesses (a state is never taken with an access in
 * flight or a file open: cardBusy). The cards' bytes are not game state:
 * image.c keeps them, and the files on disk are never rolled back. */
void cardStateIo(XgState* s) {
    XG_STATE_VAR(s, accesses);
    XG_STATE_VAR(s, started);
    XG_STATE_VAR(s, turn);
    XG_STATE_VAR(s, timeoutSlot);
    XG_STATE_VAR(s, instant);
    XG_STATE_VAR(s, files);
    XG_STATE_VAR(s, search);
    XG_STATE_VAR(s, powerOnScanDone);
}
