/* names.c -- identifier extraction from ast-grep node text */
#include <ctype.h>

#include "../trim.h"
#include "rf.h"

static int idchar(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$';
}

/* keep trailing identifier run: q..e where q is first char of the run */
static const char *ident_run_end(const char *s, const char *e,
                                 const char **start) {
    const char *q = e;
    while (q > s && idchar(q[-1]))
        q--;
    *start = q;
    return e;
}

static char *last_segment_before(const char *text, const char *paren);

/* def name = identifier token before first '('; returns NULL when no
   usable name */
char *rf_name_from_text(const char *text) {
    const char *lp = strchr(text, '(');
    if (!lp)
        return NULL;
    return last_segment_before(text, lp);
}

/* callee of a call node: its args-open paren is the '(' whose matching ')'
   is the LAST ')' in the node text (node text ends at the args close) */
char *rf_callee_from_call(const char *text) {
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1]))
        n--;
    if (!n || text[n - 1] != ')')
        return NULL;
    int depth = 0;
    const char *open = NULL;
    for (const char *p = text + n - 1; p >= text; p--) {
        if (*p == ')')
            depth++;
        else if (*p == '(' && --depth == 0) {
            open = p;
            break;
        }
    }
    if (!open)
        return NULL;
    return last_segment_before(text, open);
}

/* identifier (or trailing generic group) right before paren, as last
   dotted path segment */
static char *last_segment_before(const char *text, const char *paren) {
    const char *e = paren;
    while (e > text && (isspace((unsigned char)e[-1]) || e[-1] == '*'))
        e--;
    if (e > text && e[-1] == '>') { /* trailing generics foo<int> */
        int depth = 0;
        const char *p = e - 1;
        while (p >= text) {
            if (*p == '>')
                depth++;
            else if (*p == '<' && --depth == 0) {
                e = p;
                break;
            }
            p--;
        }
    }
    const char *s = e;
    while (s > text && idchar(s[-1]))
        s--;
    if (s == e)
        return NULL;
    size_t tl = (size_t)(e - s);
    if ((tl == 4 && !strncmp(s, "func", 4)) ||
        (tl == 3 && !strncmp(s, "fun", 3))) {
        /* go/swift/kotlin receiver: skip the receiver paren group, retry */
        const char *q = paren + 1;
        int depth = 1;
        while (*q) {
            if (*q == '(')
                depth++;
            else if (*q == ')' && --depth == 0)
                break;
            q++;
        }
        if (!*q)
            return NULL;
        return last_segment_before(q + 1, strchr(q + 1, '('));
    }
    /* walk back over the full dotted path a.b::c->d, then take last segment */
    const char *p = e;
    while (p > s) {
        char c = p[-1];
        if (idchar(c)) {
            p--;
            continue;
        }
        if ((c == '.' || c == ':') && p - 1 > s) {
            p--;
            continue;
        }
        if (c == '>' && p - 2 >= s && p[-2] == '-') {
            p -= 2;
            continue;
        }
        break;
    }
    const char *q;
    ident_run_end(p, e, &q);
    if (q == e)
        return NULL;
    size_t len = (size_t)(e - q);
    char *out = malloc(len + 1);
    memcpy(out, q, len);
    out[len] = '\0';
    return out;
}

/* last segment of a user-supplied symbol (a.b / a->b / A::b -> b) */
const char *rf_last_segment(const char *sym) {
    size_t n = strlen(sym);
    const char *q = sym + n;
    while (q > sym) {
        char c = q[-1];
        if (idchar(c) || c == '!' || c == '?') {
            q--;
            continue;
        }
        if ((c == '.' || c == ':') && q - 1 > sym) {
            q--;
            continue;
        }
        if (c == '>' && q - 2 >= sym && q[-2] == '-') {
            q -= 2;
            continue;
        }
        break;
    }
    while (q < sym + n && !idchar(*q))
        q++; /* skip any leading separator */
    return q;
}
