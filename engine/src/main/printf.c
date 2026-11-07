/*
 * Formatted output: the print hook (pAddDebugTextPrim, default addDebugTextPrim)
 * and the vprintf core behind logToScreen.
 *
 * File 0x800366E0-0x80036CD8 (main executable): vprintf's strings and jump
 * table start a new 8-aligned object at 0x80018B30. Built with GCC 2.7.2
 * (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "psyq.h"

/* The host's varargs: x86-64 passes them in registers. */
typedef __builtin_va_list va_list;
#define va_arg(ap, type) __builtin_va_arg(ap, type)

/* One printf conversion spec (flags word: bitfields; sign char in byte 1). */
typedef struct {
    u32 left : 1;    /* '-' */
    u32 plus : 1;    /* '+' */
    u32 zero : 1;    /* '0' */
    u32 hasPrec : 1; /* precision given */
    u32 : 4;
    u32 sign : 8;    /* sign character, 0 if none */
    s32 width;
    s32 prec;
    s32 base;
} PrintSpec;

static PrintSpec zeroPrintSpec;   /* all-zero spec (bss) */
void callAddDebugTextPrim(s32 c);     /* put one character through pAddDebugTextPrim */
s32 logToScreenProcessString(s32 unused, const char* fmt, va_list ap);
extern void (*pAddDebugTextPrim)(s32);

void setPrintCharCallback(void (*callback)(s32)) {
    pAddDebugTextPrim = callback;
}

void callAddDebugTextPrim(s32 c) {
    pAddDebugTextPrim(c);
}

/* Formatted output (vprintf core): each character goes to callAddDebugTextPrim.
 * Supports %[-+ 0][width|*][.prec|*] b d i u p X x c s n %. */
s32 logToScreenProcessString(s32 unused, const char* fmt, va_list ap) {
    char buf[256];
    PrintSpec spec;
    s32 count;
    s32 c;
    u32 value;
    char* p;
    char* end;
    s32 len;
    const char* digits;
    char* q;
    s32 j;

    count = 0;
    for (; (c = *fmt) != 0; fmt++) {
        if (c != '%') {
            goto literal;
        }
        spec = zeroPrintSpec;
        for (;;) {
            c = *++fmt;
            if (c == '-') {
                spec.left = 1;
            } else if (c == '+') {
                spec.plus = 1;
            } else if (c == ' ') {
                spec.sign = c;
            } else if (c == '0') {
                spec.zero = 1;
            } else {
                break;
            }
        }
        if (c == '*') {
            spec.width = va_arg(ap, s32);
            if (spec.width < 0) {
                spec.width = -spec.width;
                spec.left = 1;
            }
            c = *++fmt;
        } else {
            while ((u32)(c - '0') < 10) {
                spec.width = spec.width * 10 + (c - '0');
                c = *++fmt;
            }
        }
        if (c == '.') {
            c = *++fmt;
            if (c == '*') {
                spec.prec = va_arg(ap, s32);
                c = *++fmt;
            } else {
                while ((u32)(c - '0') < 10) {
                    spec.prec = spec.prec * 10 + (c - '0');
                    c = *++fmt;
                }
            }
            if (spec.prec >= 0) {
                spec.hasPrec = 1;
            }
        }
        p = buf + sizeof(buf);
        if (spec.left) {
            spec.zero = 0;
        }
        switch (c) {
        case 'b':
            value = va_arg(ap, u32);
            spec.sign = 0;
            spec.base = 2;
            goto number;
        case 'd':
        case 'i':
            value = va_arg(ap, s32);
            if ((s32)value < 0) {
                value = -value;
                spec.sign = '-';
            } else if (spec.plus) {
                spec.sign = '+';
            }
            spec.base = 10;
            goto number;
        case 'u':
            value = va_arg(ap, u32);
            spec.sign = 0;
            spec.base = 10;
        number:
            if (!spec.hasPrec) {
                if (spec.zero) {
                    spec.prec = spec.width;
                    if (spec.sign) {
                        spec.prec = spec.width - 1;
                    }
                }
                if (spec.prec <= 0) {
                    spec.prec = 1;
                }
            }
            len = 0;
            while (value != 0) {
                *--p = value % spec.base + '0';
                value /= spec.base;
                len++;
            }
            for (; len < spec.prec; len++) {
                *--p = '0';
            }
            if (spec.sign) {
                *--p = spec.sign;
                len++;
            }
            break;
        case 'p':
            spec.prec = 8;
            spec.hasPrec = 1;
        case 'X':
            digits = "0123456789ABCDEF";
            goto hex;
        case 'x':
            digits = "0123456789abcdef";
        hex:
            value = va_arg(ap, u32);
            if (!spec.hasPrec) {
                if (spec.zero) {
                    spec.prec = spec.width;
                }
                if (spec.prec <= 0) {
                    spec.prec = 1;
                }
            }
            len = 0;
            while (value != 0) {
                *--p = digits[value & 0xF];
                value >>= 4;
                len++;
            }
            for (; len < spec.prec; len++) {
                *--p = '0';
            }
            break;
        case 'c':
            *--p = va_arg(ap, s32);
            len = 1;
            break;
        case 's':
            p = va_arg(ap, char*);
            if (!spec.hasPrec) {
                len = (s32)strlen(p);
            } else {
                q = memchr(p, 0, spec.prec);
                if (q != NULL) {
                    len = (s32)(q - p);
                } else {
                    len = spec.prec;
                }
            }
            break;
        case 'n':
            p = va_arg(ap, char*);
            *(s32*)p = count;
            continue;
        default:
            if (c != '%') {
                return count;
            }
        literal:
            callAddDebugTextPrim(c);
            count++;
            continue;
        }
        if (len < spec.width && !spec.left) {
            do {
                callAddDebugTextPrim(' ');
                spec.width--;
                count++;
            } while (len < spec.width);
        }
        for (j = 0; j < len; j++) {
            callAddDebugTextPrim(p[j]);
        }
        count += len;
        for (; len < spec.width; len++) {
            callAddDebugTextPrim(' ');
            count++;
        }
    }
    return count;
}
