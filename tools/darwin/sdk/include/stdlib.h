#ifndef FAKE_STDLIB_H
#define FAKE_STDLIB_H
#include <stddef.h>
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *);
void abort(void) __attribute__((noreturn)); void exit(int) __attribute__((noreturn)); char *getenv(const char *);
int atoi(const char *); long strtol(const char *, char **, int);
#endif
