/* rx.h — tiny regex engine (vendored from kokke/tiny-regex-c, public
   domain / Unlicense; Rob Pike-style matcher). Split for the <200-line
   rule: types here, compile in rx_compile.c, match in rx_match.c.

   Supported syntax: '.'  '^'  '$'  '*'  '+'  '?'  '[abc]'  '[a-z]'
   '[^abc]'  '\d \D \w \W \s \S'  '\<char>' literals.  No alternation '|'
   or grouping '()'. */
#ifndef REF_RX_H
#define REF_RX_H

enum {
    RX_UNUSED,
    RX_DOT,
    RX_BEGIN,
    RX_END,
    RX_QUESTIONMARK,
    RX_STAR,
    RX_PLUS,
    RX_CHAR,
    RX_CHAR_CLASS,
    RX_INV_CHAR_CLASS,
    RX_DIGIT,
    RX_NOT_DIGIT,
    RX_ALPHA,
    RX_NOT_ALPHA,
    RX_WHITESPACE,
    RX_NOT_WHITESPACE
};

typedef struct regex_t {
    unsigned char type; /* RX_CHAR, RX_STAR, ... */
    union {
        unsigned char ch;   /* the character itself */
        unsigned char *ccl; /* OR a pointer into the char-class buffer */
    } u;
} regex_t;

typedef regex_t *re_t;

/* compile pattern; NULL on invalid/too-large pattern (static storage —
   one live pattern per process, fine for a one-shot CLI) */
re_t re_compile(const char *pattern);

/* index of first match of pattern in text, -1 if none; *matchlength set */
int re_matchp(re_t pattern, const char *text, int *matchlength);

#endif
