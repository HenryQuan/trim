/* rx_match.c -- matcher for the tiny regex engine
   (vendored from kokke/tiny-regex-c, public domain / Unlicense) */
#include <ctype.h>

#include "rx.h"

#ifndef RE_DOT_MATCHES_NEWLINE
#define RE_DOT_MATCHES_NEWLINE 1 /* '.' matches '\r' + '\n' when 1 */
#endif

static int matchone(regex_t p, char c);
static int matchstar(regex_t p, regex_t *pattern, const char *text,
                     int *matchlength);
static int matchplus(regex_t p, regex_t *pattern, const char *text,
                     int *matchlength);
static int matchquestion(regex_t p, regex_t *pattern, const char *text,
                         int *matchlength);
static int matchpattern(regex_t *pattern, const char *text, int *matchlength);

int re_matchp(re_t pattern, const char *text, int *matchlength) {
    *matchlength = 0;
    if (pattern == 0)
        return -1;
    if (pattern[0].type == RX_BEGIN)
        return matchpattern(&pattern[1], text, matchlength) ? 0 : -1;
    int idx = -1;
    do {
        idx += 1;
        if (matchpattern(pattern, text, matchlength)) {
            if (text[0] == '\0')
                return -1;
            return idx;
        }
    } while (*text++ != '\0');
    return -1;
}

static int matchdigit(char c) { return isdigit((unsigned char)c); }
static int matchalpha(char c) { return isalpha((unsigned char)c); }
static int matchwhitespace(char c) { return isspace((unsigned char)c); }
static int matchalphanum(char c) {
    return (c == '_') || matchalpha(c) || matchdigit(c);
}
static int matchrange(char c, const char *str) {
    return (c != '-') && (str[0] != '\0') && (str[0] != '-') &&
           (str[1] == '-') && (str[2] != '\0') && (c >= str[0]) &&
           (c <= str[2]);
}
static int matchdot(char c) {
#if RE_DOT_MATCHES_NEWLINE == 1
    (void)c;
    return 1;
#else
    return c != '\n' && c != '\r';
#endif
}
static int ismetachar(char c) {
    return c == 's' || c == 'S' || c == 'w' || c == 'W' || c == 'd' || c == 'D';
}
static int matchmetachar(char c, const char *str) {
    switch (str[0]) {
    case 'd':
        return matchdigit(c);
    case 'D':
        return !matchdigit(c);
    case 'w':
        return matchalphanum(c);
    case 'W':
        return !matchalphanum(c);
    case 's':
        return matchwhitespace(c);
    case 'S':
        return !matchwhitespace(c);
    default:
        return c == str[0];
    }
}
static int matchcharclass(char c, const char *str) {
    do {
        if (matchrange(c, str))
            return 1;
        if (str[0] == '\\') {
            str += 1; /* escape-char: match on next char */
            if (matchmetachar(c, str))
                return 1;
            if (c == str[0] && !ismetachar(c))
                return 1;
        } else if (c == str[0]) {
            if (c == '-')
                return str[-1] == '\0' || str[1] == '\0';
            return 1;
        }
    } while (*str++ != '\0');
    return 0;
}
static int matchone(regex_t p, char c) {
    switch (p.type) {
    case RX_DOT:
        return matchdot(c);
    case RX_CHAR_CLASS:
        return matchcharclass(c, (const char *)p.u.ccl);
    case RX_INV_CHAR_CLASS:
        return !matchcharclass(c, (const char *)p.u.ccl);
    case RX_DIGIT:
        return matchdigit(c);
    case RX_NOT_DIGIT:
        return !matchdigit(c);
    case RX_ALPHA:
        return matchalphanum(c);
    case RX_NOT_ALPHA:
        return !matchalphanum(c);
    case RX_WHITESPACE:
        return matchwhitespace(c);
    case RX_NOT_WHITESPACE:
        return !matchwhitespace(c);
    default:
        return p.u.ch == c;
    }
}
static int matchstar(regex_t p, regex_t *pattern, const char *text,
                     int *matchlength) {
    int prelen = *matchlength;
    const char *prepoint = text;
    while (text[0] != '\0' && matchone(p, *text)) {
        text++;
        (*matchlength)++;
    }
    while (text >= prepoint) {
        if (matchpattern(pattern, text--, matchlength))
            return 1;
        (*matchlength)--;
    }
    *matchlength = prelen;
    return 0;
}
static int matchplus(regex_t p, regex_t *pattern, const char *text,
                     int *matchlength) {
    const char *prepoint = text;
    while (text[0] != '\0' && matchone(p, *text)) {
        text++;
        (*matchlength)++;
    }
    while (text > prepoint) {
        if (matchpattern(pattern, text--, matchlength))
            return 1;
        (*matchlength)--;
    }
    return 0;
}
static int matchquestion(regex_t p, regex_t *pattern, const char *text,
                         int *matchlength) {
    if (p.type == RX_UNUSED)
        return 1;
    if (matchpattern(pattern, text, matchlength))
        return 1;
    if (*text && matchone(p, *text++)) {
        if (matchpattern(pattern, text, matchlength)) {
            (*matchlength)++;
            return 1;
        }
    }
    return 0;
}
/* iterative matching */
static int matchpattern(regex_t *pattern, const char *text, int *matchlength) {
    int pre = *matchlength;
    do {
        if (pattern[0].type == RX_UNUSED || pattern[1].type == RX_QUESTIONMARK)
            return matchquestion(pattern[0], &pattern[2], text, matchlength);
        if (pattern[1].type == RX_STAR)
            return matchstar(pattern[0], &pattern[2], text, matchlength);
        if (pattern[1].type == RX_PLUS)
            return matchplus(pattern[0], &pattern[2], text, matchlength);
        if (pattern[0].type == RX_END && pattern[1].type == RX_UNUSED)
            return text[0] == '\0';
        (*matchlength)++;
    } while (text[0] != '\0' && matchone(*pattern++, *text++));
    *matchlength = pre;
    return 0;
}
