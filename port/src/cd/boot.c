/*
 * Booting from disc 2. Each disc's executable embeds that disc's file table
 * and directory table (US SLUS_006.69 differs from disc 1's SLUS_006.64 only
 * there; main's discFileTable and discDirectoryTable), and the kernel uses the embedded
 * copy (initCDAndFileSystem in CD mode never reads them from the disc). The
 * PC links disc 1's executable, so when the run starts on another disc the
 * tables come from that disc's own sectors, as the game itself reloads them
 * after a disc change (the menu's checkInsertedDisc): the ID sector at LBA
 * 0x17 ("DS0n_XENOGEARS"), the file table at LBA 0x18 (0x8000 bytes) and the
 * directory table at LBA 0x28 (0x7A bytes). kernelMain passes its tables
 * (PLATFORM_PC) just before initCDAndFileSystem.
 */

#include <stdio.h>
#include <string.h>

#include "cd.h"
#include "../system/savestate.h"

#define USER_OFFSET 24 /* Mode 2 Form 1: sync, header, subheader */
#define USER_SIZE 2048

static int readUser(int lba, unsigned char* out, int size) {
    uint8_t raw[CD_RAW_SECTOR];

    for (int done = 0; done < size; done += USER_SIZE, lba++) {
        if (!cdReadRawSector(lba, raw)) {
            return 0;
        }
        memcpy(out + done, raw + USER_OFFSET, (size_t)(size - done < USER_SIZE ? size - done : USER_SIZE));
    }
    return 1;
}

int cdBootDiscNumber(void) {
    unsigned char id[16];

    if (!readUser(0x17, id, sizeof id) || memcmp(id + 4, "_XEN", 4) != 0 || id[3] < '1' || id[3] > '9') {
        return 0;
    }
    return id[3] - '0';
}

static int reload;
static int tablesDone; /* the first boot's tables are in place */

void cdBootTablesReload(void) {
    reload = 1;
}

void cdBootTables(uint8_t* fileTable, uint16_t* directoryTable) {
    static unsigned char table[0x8000];
    static unsigned char dirs[0x7A];
    int disc;

    if (tablesDone && !reload) {
        return; /* a soft reset keeps the executable's data, as on the PS1 */
    }
    int again = reload;
    tablesDone = 1;
    reload = 0;
    disc = cdBootDiscNumber();
    if (disc < 1 || (disc == 1 && !again)) {
        return; /* disc 1 (the linked executable's own tables), or not a Xenogears disc */
    }
    if (!readUser(0x18, table, sizeof table) || !readUser(0x28, dirs, sizeof dirs)) {
        fprintf(stderr, "cd: cannot read disc %d's file tables\n", disc);
        return;
    }
    memcpy(fileTable, table, sizeof table);
    memcpy(directoryTable, dirs, sizeof dirs);
    fprintf(stderr, "cd: booting disc %d with its own file tables\n", disc);
}

/* Save states (system/savestate.h): whether a reset reads the tables again. */
void bootStateIo(XgState* s) {
    XG_STATE_VAR(s, reload);
    XG_STATE_VAR(s, tablesDone);
}
