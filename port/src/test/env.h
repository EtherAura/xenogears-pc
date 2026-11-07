#ifndef PORT_TEST_ENV_H
#define PORT_TEST_ENV_H

#include <SDL3/SDL.h>
#include <stdlib.h>

/* Keep the executable's CRT and SDL's UTF-8 environment in step. SDL takes
 * a snapshot on first use; later fixture edits must update that snapshot. */
static inline int testSetEnv(const char* name, const char* value) {
#ifdef _WIN32
    if (_putenv_s(name, value)) return -1;
#else
    if (SDL_setenv_unsafe(name, value, 1)) return -1;
#endif
    return SDL_SetEnvironmentVariable(SDL_GetEnvironment(),name,value,true) ? 0 : -1;
}

#endif
