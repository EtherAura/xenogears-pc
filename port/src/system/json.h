#ifndef PORT_SYSTEM_JSON_H
#define PORT_SYSTEM_JSON_H

/*
 * A JSON reader (json.c) for the canonical states' schema (stateschema.c):
 * the whole document parsed into nodes in document order. A node's children
 * follow it; jsonNext skips a node and everything under it. Strings come
 * back decoded (escapes and \u sequences as UTF-8) and NUL-terminated;
 * numbers without a fraction or exponent are read exactly as 64-bit
 * integers.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_INT, JSON_REAL, JSON_STRING, JSON_ARRAY, JSON_OBJECT };

typedef struct JsonDoc JsonDoc;
typedef int32_t JsonRef; /* a node; JSON_NONE for none */
#define JSON_NONE (-1)

/* Parses n bytes; NULL on an error, described in err (with its byte offset). */
JsonDoc* jsonParse(const char* text, size_t n, char* err, size_t errSize);
void jsonFree(JsonDoc* d);

JsonRef jsonRoot(const JsonDoc* d);
int jsonType(const JsonDoc* d, JsonRef r);
/* An array's or object's first entry (an object's: its first value); JSON_NONE if empty. */
JsonRef jsonFirst(const JsonDoc* d, JsonRef r);
/* The entry after r in its array or object, JSON_NONE after the last. */
JsonRef jsonNext(const JsonDoc* d, JsonRef r);
/* The key of an object's value r ("" for an array entry). */
const char* jsonKey(const JsonDoc* d, JsonRef r);
/* An object's value for key, JSON_NONE if absent (a linear search). */
JsonRef jsonGet(const JsonDoc* d, JsonRef obj, const char* key);
int32_t jsonCount(const JsonDoc* d, JsonRef r);
/* A string node's text ("" for another kind). */
const char* jsonString(const JsonDoc* d, JsonRef r);
/* A number's value (0 for another kind); JSON_TRUE is 1. */
int64_t jsonInt(const JsonDoc* d, JsonRef r);
double jsonReal(const JsonDoc* d, JsonRef r);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_JSON_H */
