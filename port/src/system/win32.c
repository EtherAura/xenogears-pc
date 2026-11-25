/*
 * Windows-only parts of the platform layer.
 *
 * (Battling's coroutine and the game's own stack switch stacks with a few
 * instructions of assembly on Windows as on Linux, keeping the TEB's stack
 * bounds as a fiber switch would: port/src/game/battling/thread.c,
 * port/src/system/gamestack.c. Their stacks are static arrays, which save
 * states hold.)
 */

#ifdef _WIN32

#include <windows.h>
#include <io.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "system/system.h"

/* The game calls bzero, which glibc has and the Windows C runtime lacks. */
void bzero(void* p, size_t n) {
    memset(p, 0, n);
}

static int usable(HANDLE h) {
    return h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

/* xenogears.exe is a GUI program (port/CMakeLists.txt WIN32_EXECUTABLE), so a start from Explorer or
 * the Start menu opens no console window. Started from a terminal, its log goes where a console
 * program's would: to the standard handles it was given (a file, a pipe: kept as they are), else to
 * the console of the process that started it. Windows' shells do not wait for a GUI program, so
 * there the log can come after the prompt ("start /wait xenogears.exe" waits). Called first in main,
 * before anything is written and before the C runtime's Ctrl+C and Ctrl+Break handlers are set. */
void win32AttachConsole(void) {
    int out = usable(GetStdHandle(STD_OUTPUT_HANDLE)), err = usable(GetStdHandle(STD_ERROR_HANDLE));
    if ((out && err) || !AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    /* The process's own handles too, which a process the game starts inherits (system/engine.c). */
    if (!out && freopen("CONOUT$", "w", stdout) != NULL) {
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    }
    if (!err && freopen("CONOUT$", "w", stderr) != NULL) {
        setvbuf(stderr, NULL, _IONBF, 0);
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
    }
}

#endif /* _WIN32 */
