/* Local link checks only: declarations, no Apple SDK. */
#ifndef FAKE_STDIO_H
#define FAKE_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct FAKE_FILE FILE;
extern FILE *__stderrp, *__stdoutp;
#define stderr __stderrp
#define stdout __stdoutp
#define SEEK_SET 0
#define SEEK_END 2
#define EOF (-1)
int printf(const char *, ...); int fprintf(FILE *, const char *, ...); int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...); int vsnprintf(char *, size_t, const char *, va_list);
FILE *fopen(const char *, const char *); int fclose(FILE *); int fseek(FILE *, long, int); long ftell(FILE *);
int fgetc(FILE *); size_t fread(void *, size_t, size_t, FILE *); size_t fwrite(const void *, size_t, size_t, FILE *);
int fputs(const char *, FILE *); int puts(const char *); int fflush(FILE *);
#endif
