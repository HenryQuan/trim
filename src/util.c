#include "trim.h"

size_t MAX_CHARS = 5120;
size_t MAX_LINES = 512;

void init_max_chars(void) {
    char *env = getenv("TRIM_MAX_CHARS");
    if (env) {
        unsigned long v = strtoul(env, NULL, 10);
        if (v > 0) MAX_CHARS = v < 65536 ? (size_t)v : 65536;
    }
    char *envl = getenv("TRIM_MAX_LINES");
    if (envl) {
        unsigned long v = strtoul(envl, NULL, 10);
        if (v > 0) MAX_LINES = v < 100000 ? (size_t)v : 100000;
    }
}

const char *pick_hint(const char *cmd) {
    if (!strncmp(cmd, "rg ", 3)) return HINT_RG;
    if (!strncmp(cmd, "ast-grep", 8)) return HINT_OUT;
    return HINT_PAR;
}

int is_read(const char *s) {
    return !strcmp(s, "read") || !strcmp(s, "cat") || !strcmp(s, "print");
}

int is_lines(const char *s) {
    return !strcmp(s, "lines") || !strcmp(s, "l") || !strcmp(s, "sed");
}

static const char *HINT =
    "\n[TRUNCATED:%zu/%zuc] %s\n";

void cap(const char *s, int truncated, size_t total_chars, const char *hint) {
    size_t len = strlen(s);
    if (truncated || len > MAX_CHARS) {
        size_t n = len < MAX_CHARS ? len : MAX_CHARS;
        fwrite(s, 1, n, stdout);
        printf(HINT, MAX_CHARS, total_chars, hint ? hint : HINT_PAR);
    } else {
        fputs(s, stdout);
    }
}

/* strip ANSI escape codes (ESC[<params><letter>) in place; returns new length */
size_t strip_ansi(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0x1b && i + 1 < n && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')) j++;
            if (j < n && ((s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= 'a' && s[j] <= 'z'))) {
                i = j;
                continue;
            }
        }
        s[w++] = s[i];
    }
    return w;
}

/* collapse runs of 3+ newlines into 2 in place; returns new length */
size_t collapse_blanks(char *s, size_t n) {
    size_t w = 0, nl = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') {
            nl++;
            if (nl > 2) continue;
        } else {
            nl = 0;
        }
        s[w++] = s[i];
    }
    return w;
}

/* normalize \r\n -> \n and lone \r -> \n in place; returns new length */
size_t normalize_newlines(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') continue;
            s[w++] = '\n';
        } else {
            s[w++] = s[i];
        }
    }
    return w;
}

/* collapse literal backslash escapes (\r\n, \n, \r, \t) to a single space; returns new length */
size_t collapse_escapes(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[i + 1];
            if (c == 'n' || c == 't' || c == 'r') {
                if (c == 'r' && i + 2 < n && s[i + 2] == 'n') i++; /* \r\n -> skip the n too */
                s[w++] = ' ';
                i++;
                continue;
            }
        }
        s[w++] = s[i];
    }
    return w;
}

/* strip leading spaces/tabs in place (NUL-terminated); returns new length */
size_t lstrip(char *s) {
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s) - i + 1);
    return strlen(s);
}

/* strip leading spaces/tabs from every line in a buffer; returns new length */
size_t lstrip_lines(char *s, size_t n) {
    size_t w = 0, i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != '\n') j++;
        size_t k = i;
        while (k < j && (s[k] == ' ' || s[k] == '\t')) k++;
        size_t len = j - k;
        memmove(s + w, s + k, len);
        w += len;
        if (j < n) s[w++] = '\n';
        i = j + 1;
    }
    return w;
}

/* collapse runs of 2+ spaces/tabs to one space in place; returns new length */
size_t collapse_spaces(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if ((s[i] == ' ' || s[i] == '\t') && w > 0 && (s[w - 1] == ' ' || s[w - 1] == '\t'))
            continue;
        s[w++] = s[i];
    }
    return w;
}

/* trim trailing spaces/tabs/\r from every line in place; returns new length */
size_t rtrim_lines(char *s, size_t n) {
    size_t w = 0, i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != '\n') j++;
        size_t e = j;
        while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
        size_t len = e - i;
        memmove(s + w, s + i, len);
        w += len;
        if (j < n) s[w++] = '\n';
        i = j + 1;
    }
    return w;
}

int is_blank_line(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return *s == '\0';
}
