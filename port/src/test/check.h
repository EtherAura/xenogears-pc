#ifndef PORT_TEST_CHECK_H
#define PORT_TEST_CHECK_H

/*
 * The port's unit tests' checks, in one place (xg-htn.34.9.3: each test had
 * its own copy).
 *
 *   CHECK(x)              counts a check; when x is false, says where on
 *                         stderr ("line N: x") and returns 1 from the calling
 *                         function, the test's failure status
 *   EXPECT(x)             the same, but counts a failure and goes on
 *   EXPECTF(x, fmt, ...)  EXPECT with a printf-style note after the check
 *
 * The test defines the counters: `static int checks;`, and with EXPECT
 * `static int failures;` (its exit status follows from it). Before including
 * this it may define
 *   CHECK_DETAIL   a string to add to a failure in parentheses (SDL_GetError()
 *                  in the tests of SDL calls)
 *   CHECK_CONTEXT  an expression that prints where a loop was, before "line N"
 */

#include <stdio.h>

#ifndef CHECK_CONTEXT
#define CHECK_CONTEXT ((void)0)
#endif

#ifdef CHECK_DETAIL
#define CHECK_SAY(what) (CHECK_CONTEXT, fprintf(stderr, "line %d: %s (%s)", __LINE__, what, CHECK_DETAIL))
#else
#define CHECK_SAY(what) (CHECK_CONTEXT, fprintf(stderr, "line %d: %s", __LINE__, what))
#endif

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(x)) {                                                                                                    \
            CHECK_SAY(#x);                                                                                             \
            fputc('\n', stderr);                                                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

#define EXPECT(x)                                                                                                      \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(x)) {                                                                                                    \
            failures++;                                                                                                \
            CHECK_SAY(#x);                                                                                             \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

#define EXPECTF(x, ...)                                                                                                \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(x)) {                                                                                                    \
            failures++;                                                                                                \
            CHECK_SAY(#x);                                                                                             \
            fputs(": ", stderr);                                                                                       \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

#endif
