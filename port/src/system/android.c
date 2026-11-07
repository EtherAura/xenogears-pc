/*
 * Android-only parts of the platform layer (xg-mrj.3.6). The app itself is
 * port/android: its loader (port/android/loader/xgloader.c) sets the start
 * options and loads this library.
 */

#ifdef __ANDROID__

#include <stddef.h>
#include <string.h>

/* The game calls bzero, which glibc has and bionic dropped (as the Windows
 * C runtime lacks it: port/src/system/win32.c). */
void bzero(void* p, size_t n) {
    memset(p, 0, n);
}

#endif
