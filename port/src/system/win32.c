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

#include <stddef.h>
#include <string.h>

/* The game calls bzero, which glibc has and the Windows C runtime lacks. */
void bzero(void* p, size_t n) {
    memset(p, 0, n);
}

#endif /* _WIN32 */
