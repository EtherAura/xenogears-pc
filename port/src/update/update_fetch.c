/* The updater's GETs (update_check.h updatePlatformFetch, updatePlatformDownload):
 * the system's own HTTPS, never a TLS library of ours. Each runs on the
 * updater's own thread, https only, follows redirects to https (GitHub's
 * latest/download/<name> answers with one to its download host), gives up
 * after 15 s without a connection, and refuses a body longer than asked for. A
 * fetch (the manifest, its signature) gives up after 30 s in all; a download
 * (a release's archive) only when it stalls (under 1 KB/s for 30 s), or when
 * the player cancels it. The request carries no version and nothing about the
 * player: its User-Agent is "xenogears-pc".
 *
 *   Windows   WinHTTP (winhttp.dll), the system's proxy settings
 *   Android   the activity's updateFetch (XgActivity.java: HttpURLConnection),
 *             the whole body at once
 *   others    libcurl, loaded when first needed (libcurl.so.4; on macOS
 *             libcurl.4.dylib): none installed, no check. Its option numbers
 *             are libcurl's public ABI (curl/curl.h), written out so that no
 *             build needs its headers.
 *
 * Each platform's request() writes the body through a Sink: into memory (a
 * fetch) or into a file, with the progress (a download). */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_check.h"

#define AGENT "xenogears-pc"

static void say(char* why, size_t size, const char* fmt, ...) {
    if (why == NULL || size == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

/* ---- where a body goes ---------------------------------------------------------------------------- */

typedef struct Sink {
    int (*put)(struct Sink* s, const uint8_t* data, size_t n); /* 0 stops the request (why set) */
    uint64_t max, got;
    int stopped;     /* put said stop: why is its reason */
    char* why;
    size_t whySize;
    char* buf;       /* a fetch */
    SDL_IOStream* io; /* a download */
    UpdateProgress progress;
    void* user;
} Sink;

static int putMemory(Sink* s, const uint8_t* data, size_t n) {
    if (s->got + n > s->max) {
        say(s->why, s->whySize, "longer than %llu bytes", (unsigned long long)s->max);
        return 0;
    }
    memcpy(s->buf + s->got, data, n);
    s->got += n;
    return 1;
}

static int putFile(Sink* s, const uint8_t* data, size_t n) {
    if (s->got + n > s->max) {
        say(s->why, s->whySize, "longer than the manifest says (%llu bytes)", (unsigned long long)s->max);
        return 0;
    }
    if (SDL_WriteIO(s->io, data, n) != n) {
        say(s->why, s->whySize, "cannot write the download: %s", SDL_GetError());
        return 0;
    }
    s->got += n;
    if (s->progress != NULL && !s->progress(s->user, s->got, s->max)) {
        say(s->why, s->whySize, "cancelled");
        return 0;
    }
    return 1;
}

static int sinkPut(Sink* s, const uint8_t* data, size_t n) {
    if (s->stopped) return 0;
    if (n > 0 && !s->put(s, data, n)) s->stopped = 1;
    return !s->stopped;
}

/* request(url, sink, download, why): the body's size through the sink, -1 with why, -2 for HTTP 404. */
static long request(const char* url, Sink* sink, int download, char* why, size_t whySize);

#ifdef XG_UPDATER_TEST
/* A test build of the updater (-DXG_UPDATER_TEST=ON, never a release): with XG_UPDATE_TEST_DIR set, a
 * URL's last part is read from that folder instead (missing: HTTP 404), in pieces, for the progress. */
static long testRequest(const char* dir, const char* url, Sink* sink, char* why, size_t whySize) {
    const char* name = strrchr(url, '/');
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name != NULL ? name + 1 : url);
    size_t n = 0;
    uint8_t* bytes = (uint8_t*)SDL_LoadFile(path, &n);
    if (bytes == NULL) {
        say(why, whySize, "HTTP 404 (%s)", path);
        return -2;
    }
    const char* pause = SDL_getenv("XG_UPDATE_TEST_DELAY"); /* ms after each 64 KB: the progress seen */
    Uint32 ms = pause != NULL ? (Uint32)SDL_atoi(pause) : 0;
    for (size_t at = 0; at < n; at += 65536) {
        if (!sinkPut(sink, bytes + at, n - at < 65536 ? n - at : 65536)) {
            SDL_free(bytes);
            return -1;
        }
        if (ms > 0) SDL_Delay(ms);
    }
    SDL_free(bytes);
    return (long)sink->got;
}
#endif

static long requestAny(const char* url, Sink* sink, int download, char* why, size_t whySize) {
#ifdef XG_UPDATER_TEST
    const char* dir = SDL_getenv("XG_UPDATE_TEST_DIR");
    if (dir != NULL && dir[0] != '\0') return testRequest(dir, url, sink, why, whySize);
#endif
    return request(url, sink, download, why, whySize);
}

static long fetchMemory(const char* url, char* buf, size_t max, char* why, size_t whySize) {
    Sink s = {putMemory, max, 0, 0, why, whySize, buf, NULL, NULL, NULL};
    return requestAny(url, &s, 0, why, whySize);
}

static int downloadFile(const char* url, const char* path, uint64_t size, UpdateProgress progress, void* user,
                        char* why, size_t whySize) {
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io == NULL) {
        say(why, whySize, "cannot create %s: %s", path, SDL_GetError());
        return 0;
    }
    Sink s = {putFile, size, 0, 0, why, whySize, NULL, io, progress, user};
    long n = requestAny(url, &s, 1, why, whySize);
    int closed = SDL_CloseIO(io);
    if (n == -2) say(why, whySize, "%s is not there (HTTP 404)", url);
    if (n < 0) return 0;
    if (!closed) {
        say(why, whySize, "cannot write %s: %s", path, SDL_GetError());
        return 0;
    }
    if ((uint64_t)n != size) {
        say(why, whySize, "the download is %ld bytes, not the %llu its manifest says", n, (unsigned long long)size);
        return 0;
    }
    return 1;
}

UpdateDownload updatePlatformDownload(void) {
    return updatePlatformFetch(NULL) != NULL ? downloadFile : NULL;
}

#if defined(_WIN32)

#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4 /* Windows 8.1 and later */
#endif

static void lastError(const char* what, char* why, size_t whySize) {
    say(why, whySize, "%s failed (WinHTTP error %lu)", what, (unsigned long)GetLastError());
}

static long request(const char* url, Sink* sink, int download, char* why, size_t whySize) {
    wchar_t wurl[1024], host[256], path[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024) == 0) {
        say(why, whySize, "the URL is too long");
        return -1;
    }
    URL_COMPONENTS parts;
    memset(&parts, 0, sizeof parts);
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        say(why, whySize, "not an https URL");
        return -1;
    }
    long result = -1;
    HINTERNET session = WinHttpOpen(L"" AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) { /* before Windows 8.1 */
        session = WinHttpOpen(L"" AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    HINTERNET connection = NULL, req = NULL;
    if (session == NULL) {
        lastError("WinHttpOpen", why, whySize);
        goto done;
    }
    /* Resolving and connecting 15 s each, sending 30 s; each read 30 s (a fetch) or 60 s (a download). */
    WinHttpSetTimeouts(session, 15000, 15000, 30000, download ? 60000 : 30000);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == NULL) {
        lastError("WinHttpConnect", why, whySize);
        goto done;
    }
    req = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE);
    if (req == NULL) {
        lastError("WinHttpOpenRequest", why, whySize);
        goto done;
    }
    /* Redirects are followed, never from https to http (WinHTTP's default policy, set here all the same). */
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        lastError("the request", why, whySize);
        goto done;
    }
    if (!WinHttpReceiveResponse(req, NULL)) {
        lastError("the response", why, whySize);
        goto done;
    }
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        lastError("the status", why, whySize);
        goto done;
    }
    if (status != 200) {
        say(why, whySize, "HTTP %lu", (unsigned long)status);
        result = status == 404 ? -2 : -1;
        goto done;
    }
    for (;;) {
        DWORD n = 0;
        uint8_t chunk[16384];
        if (!WinHttpReadData(req, chunk, sizeof chunk, &n)) {
            lastError("reading the body", why, whySize);
            goto done;
        }
        if (n == 0) break;
        if (!sinkPut(sink, chunk, n)) goto done;
    }
    result = (long)sink->got;
done:
    if (req != NULL) WinHttpCloseHandle(req);
    if (connection != NULL) WinHttpCloseHandle(connection);
    if (session != NULL) WinHttpCloseHandle(session);
    return result;
}

UpdateFetch updatePlatformFetch(const char** name) {
    if (name != NULL) *name = "WinHTTP";
    return fetchMemory;
}

int updateFetchAbort(void) {
    return 0; /* ExitProcess ends the other threads before any DLL's cleanup runs */
}

#elif defined(__ANDROID__)

#include <jni.h>

/* A Java exception's text (its toString: "java.io.IOException: HTTP 404"). */
static void exceptionText(JNIEnv* env, jthrowable e, char* why, size_t whySize) {
    say(why, whySize, "the request failed");
    jclass c = (*env)->GetObjectClass(env, e);
    jmethodID toString = c != NULL ? (*env)->GetMethodID(env, c, "toString", "()Ljava/lang/String;") : NULL;
    jstring s = toString != NULL ? (jstring)(*env)->CallObjectMethod(env, e, toString) : NULL;
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (s != NULL) {
        const char* t = (*env)->GetStringUTFChars(env, s, NULL);
        if (t != NULL) {
            say(why, whySize, "%s", t);
            (*env)->ReleaseStringUTFChars(env, s, t);
        }
        (*env)->DeleteLocalRef(env, s);
    }
    if (c != NULL) (*env)->DeleteLocalRef(env, c);
}

/* The activity's updateFetch hands back the whole body: it goes through the sink at once. */
static long request(const char* url, Sink* sink, int download, char* why, size_t whySize) {
    (void)download;
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv(); /* attaches this thread */
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (env == NULL || activity == NULL) {
        say(why, whySize, "no activity");
        return -1;
    }
    long result = -1;
    jclass c = (*env)->GetObjectClass(env, activity);
    jmethodID m = (*env)->GetMethodID(env, c, "updateFetch", "(Ljava/lang/String;I)[B");
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        say(why, whySize, "the app has no updateFetch");
        goto done;
    }
    jstring jurl = (*env)->NewStringUTF(env, url);
    jint max = sink->max > 0x7fffffff ? 0x7fffffff : (jint)sink->max;
    jbyteArray body = (jbyteArray)(*env)->CallObjectMethod(env, activity, m, jurl, max);
    jthrowable e = (*env)->ExceptionOccurred(env);
    if (e != NULL) {
        (*env)->ExceptionClear(env);
        exceptionText(env, e, why, whySize);
        (*env)->DeleteLocalRef(env, e);
        /* XgActivity.updateFetch: "java.io.IOException: HTTP 404" for a file that is not there */
        if (strstr(why, "HTTP 404") != NULL) result = -2;
    } else if (body != NULL) {
        jsize n = (*env)->GetArrayLength(env, body);
        jbyte* bytes = (*env)->GetByteArrayElements(env, body, NULL);
        if (bytes != NULL) {
            if (sinkPut(sink, (const uint8_t*)bytes, (size_t)n)) result = n;
            (*env)->ReleaseByteArrayElements(env, body, bytes, JNI_ABORT);
        }
        (*env)->DeleteLocalRef(env, body);
    }
    if (jurl != NULL) (*env)->DeleteLocalRef(env, jurl);
done:
    (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, activity);
    return result;
}

UpdateFetch updatePlatformFetch(const char** name) {
    if (name != NULL) *name = "Android HttpURLConnection";
    return fetchMemory;
}

int updateFetchAbort(void) {
    return 0; /* the request is Java's; the app's end (quit.c) runs no other library's exit handlers */
}

#else

/* libcurl's public ABI (curl/curl.h): option numbers are a type's base plus the option's own. */
enum {
    CURLOPT_TIMEOUT = 13,
    CURLOPT_LOW_SPEED_LIMIT = 19,
    CURLOPT_LOW_SPEED_TIME = 20,
    CURLOPT_NOPROGRESS = 43,
    CURLOPT_FAILONERROR = 45,
    CURLOPT_FOLLOWLOCATION = 52,
    CURLOPT_MAXREDIRS = 68,
    CURLOPT_CONNECTTIMEOUT = 78,
    CURLOPT_NOSIGNAL = 99,
    CURLOPT_MAXFILESIZE = 114,
    CURLOPT_PROTOCOLS = 181,       /* before 7.85.0; CURLPROTO_HTTPS = 1 << 1 */
    CURLOPT_REDIR_PROTOCOLS = 182, /* the same */
    CURLOPT_WRITEDATA = 10001,
    CURLOPT_URL = 10002,
    CURLOPT_ERRORBUFFER = 10010,
    CURLOPT_USERAGENT = 10018,
    CURLOPT_PROTOCOLS_STR = 10318,       /* 7.85.0 */
    CURLOPT_REDIR_PROTOCOLS_STR = 10319, /* 7.85.0 */
    CURLOPT_WRITEFUNCTION = 20011,
    CURLOPT_XFERINFOFUNCTION = 20219,
    CURLINFO_RESPONSE_CODE = 0x200002,
    CURLE_OK = 0,
    CURLE_HTTP_RETURNED_ERROR = 22,
    CURL_GLOBAL_DEFAULT = 3,
    CURL_ERROR_SIZE = 256,
    CURLPROTO_HTTPS = 1 << 1,
};

typedef void CURL;
static struct {
    int tried;
    SDL_SharedObject* lib;
    int (*globalInit)(long flags);
    CURL* (*init)(void);
    int (*setopt)(CURL* curl, int option, ...);
    int (*perform)(CURL* curl);
    int (*getinfo)(CURL* curl, int info, ...);
    void (*cleanup)(CURL* curl);
    const char* (*strerror)(int code);
    const char* (*version)(void);
    char name[160];
} C;

static void loadCurl(void) {
    if (C.tried) return;
    C.tried = 1;
#ifdef __APPLE__
    static const char* const NAMES[] = {"libcurl.4.dylib", "/usr/lib/libcurl.4.dylib"};
#else
    static const char* const NAMES[] = {"libcurl.so.4", "libcurl-gnutls.so.4"};
#endif
    for (size_t i = 0; i < SDL_arraysize(NAMES) && C.lib == NULL; i++) C.lib = SDL_LoadObject(NAMES[i]);
    if (C.lib == NULL) {
        snprintf(C.name, sizeof C.name, "no libcurl (%s) on this system", NAMES[0]);
        return;
    }
#define SYM(field, symbol) *(void**)&C.field = (void*)SDL_LoadFunction(C.lib, symbol)
    SYM(globalInit, "curl_global_init");
    SYM(init, "curl_easy_init");
    SYM(setopt, "curl_easy_setopt");
    SYM(perform, "curl_easy_perform");
    SYM(getinfo, "curl_easy_getinfo");
    SYM(cleanup, "curl_easy_cleanup");
    SYM(strerror, "curl_easy_strerror");
    SYM(version, "curl_version");
#undef SYM
    if (C.globalInit == NULL || C.init == NULL || C.setopt == NULL || C.perform == NULL || C.getinfo == NULL ||
        C.cleanup == NULL || C.strerror == NULL || C.version == NULL || C.globalInit(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        snprintf(C.name, sizeof C.name, "libcurl is not usable");
        SDL_UnloadObject(C.lib);
        C.lib = NULL;
        return;
    }
    snprintf(C.name, sizeof C.name, "%s", C.version());
}

static size_t curlWrite(const char* data, size_t size, size_t count, void* user) {
    size_t n = size * count;
    return sinkPut((Sink*)user, (const uint8_t*)data, n) ? n : 0; /* 0: CURLE_WRITE_ERROR */
}

/* updateFetchAbort: libcurl calls this about once a second, or sooner, while a request runs; non-zero
 * ends it (CURLE_ABORTED_BY_CALLBACK). */
static SDL_AtomicInt aborting;

static int progress(void* user, int64_t total, int64_t now, int64_t upTotal, int64_t upNow) {
    (void)user, (void)total, (void)now, (void)upTotal, (void)upNow;
    return SDL_GetAtomicInt(&aborting) != 0;
}

static long request(const char* url, Sink* sink, int download, char* why, size_t whySize) {
    CURL* curl = C.init();
    if (curl == NULL) {
        say(why, whySize, "curl_easy_init failed");
        return -1;
    }
    char error[CURL_ERROR_SIZE] = "";
    C.setopt(curl, CURLOPT_URL, url);
    C.setopt(curl, CURLOPT_USERAGENT, AGENT);
    C.setopt(curl, CURLOPT_NOSIGNAL, 1L); /* a thread of its own: no SIGALRM for its timeouts */
    C.setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    C.setopt(curl, CURLOPT_MAXREDIRS, 5L);
    C.setopt(curl, CURLOPT_FAILONERROR, 1L);
    C.setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    if (download) { /* as long as it takes, while it moves */
        C.setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        C.setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    } else {
        C.setopt(curl, CURLOPT_TIMEOUT, 30L);
    }
    C.setopt(curl, CURLOPT_MAXFILESIZE, (long)sink->max);
    C.setopt(curl, CURLOPT_ERRORBUFFER, error);
    C.setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    C.setopt(curl, CURLOPT_WRITEDATA, sink);
    C.setopt(curl, CURLOPT_NOPROGRESS, 0L);
    C.setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    if (C.setopt(curl, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK ||
        C.setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https") != CURLE_OK) {
        C.setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS); /* a libcurl before 7.85.0 */
        C.setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    }
    int code = C.perform(curl);
    long status = 0;
    C.getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    C.cleanup(curl);
    if (code == CURLE_OK) return (long)sink->got;
    if (sink->stopped) return -1; /* the sink's reason */
    if (code == CURLE_HTTP_RETURNED_ERROR) {
        say(why, whySize, "HTTP %ld", status);
        return status == 404 ? -2 : -1;
    }
    say(why, whySize, "%s", error[0] ? error : C.strerror(code));
    return -1;
}

UpdateFetch updatePlatformFetch(const char** name) {
    loadCurl();
    if (name != NULL) *name = C.name;
    return C.lib != NULL ? fetchMemory : NULL;
}

/* The process is ending: a request under way gives up within a second, before exit() runs libcurl's
 * TLS library's exit handlers under it. */
int updateFetchAbort(void) {
    SDL_SetAtomicInt(&aborting, 1);
    return 1;
}

#endif
