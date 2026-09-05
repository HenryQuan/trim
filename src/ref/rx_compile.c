/* rx_compile.c -- pattern compiler for the tiny regex engine
   (vendored from kokke/tiny-regex-c, public domain / Unlicense) */
#include "rx.h"

#define MAX_REGEXP_OBJECTS 30 /* max number of regex symbols in expression */
#define MAX_CHAR_CLASS_LEN 40 /* max length of character-class buffer */

re_t re_compile(const char *pattern) {
    static regex_t re_compiled[MAX_REGEXP_OBJECTS];
    static unsigned char ccl_buf[MAX_CHAR_CLASS_LEN];
    int ccl_bufidx = 1;

    char c;    /* current char in pattern */
    int i = 0; /* index into pattern */
    int j = 0; /* index into re_compiled */

    while (pattern[i] != '\0' && (j + 1 < MAX_REGEXP_OBJECTS)) {
        c = pattern[i];

        switch (c) {
        /* meta-characters */
        case '^':
            re_compiled[j].type = RX_BEGIN;
            break;
        case '$':
            re_compiled[j].type = RX_END;
            break;
        case '.':
            re_compiled[j].type = RX_DOT;
            break;
        case '*':
            re_compiled[j].type = RX_STAR;
            break;
        case '+':
            re_compiled[j].type = RX_PLUS;
            break;
        case '?':
            re_compiled[j].type = RX_QUESTIONMARK;
            break;

        /* escaped character-classes (\s \w ...) and literals (\.) */
        case '\\':
            if (pattern[i + 1] != '\0') {
                i += 1; /* skip the escape-char */
                switch (pattern[i]) {
                case 'd':
                    re_compiled[j].type = RX_DIGIT;
                    break;
                case 'D':
                    re_compiled[j].type = RX_NOT_DIGIT;
                    break;
                case 'w':
                    re_compiled[j].type = RX_ALPHA;
                    break;
                case 'W':
                    re_compiled[j].type = RX_NOT_ALPHA;
                    break;
                case 's':
                    re_compiled[j].type = RX_WHITESPACE;
                    break;
                case 'S':
                    re_compiled[j].type = RX_NOT_WHITESPACE;
                    break;
                default:
                    re_compiled[j].type = RX_CHAR;
                    re_compiled[j].u.ch = pattern[i];
                    break;
                }
            } else {
                return 0; /* '\\' as last char -> invalid expression */
            }
            break;

        /* character class */
        case '[': {
            int buf_begin = ccl_bufidx;
            if (pattern[i + 1] == '^') {
                re_compiled[j].type = RX_INV_CHAR_CLASS;
                i += 1; /* avoid including '^' in the char buffer */
                if (pattern[i + 1] == 0)
                    return 0; /* incomplete pattern */
            } else {
                re_compiled[j].type = RX_CHAR_CLASS;
            }
            /* copy characters inside [..] to buffer */
            while ((pattern[++i] != ']') && (pattern[i] != '\0')) {
                if (pattern[i] == '\\') {
                    if (ccl_bufidx >= MAX_CHAR_CLASS_LEN - 1)
                        return 0;
                    if (pattern[i + 1] == 0)
                        return 0; /* incomplete pattern */
                    ccl_buf[ccl_bufidx++] = pattern[i++];
                } else if (ccl_bufidx >= MAX_CHAR_CLASS_LEN) {
                    return 0;
                }
                ccl_buf[ccl_bufidx++] = pattern[i];
            }
            if (ccl_bufidx >= MAX_CHAR_CLASS_LEN)
                return 0;
            ccl_buf[ccl_bufidx++] = 0; /* null-terminate class string */
            re_compiled[j].u.ccl = &ccl_buf[buf_begin];
            break;
        }

        /* other characters */
        default:
            re_compiled[j].type = RX_CHAR;
            re_compiled[j].u.ch = c;
            break;
        }
        /* no buffer-out-of-bounds access on invalid patterns */
        if (pattern[i] == 0)
            return 0;

        i += 1;
        j += 1;
    }
    re_compiled[j].type = RX_UNUSED; /* sentinel: end-of-pattern */

    return (re_t)re_compiled;
}
