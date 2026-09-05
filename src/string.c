/* string.c — trim string <text> [path]
   Find a UI string, resolve the identifier bound to it on the same line
   (i18n key in JSON/ARB/YAML/properties/XML; the key is optional — a
   hardcoded string has none), then report every translation of that key
   plus every code reference, each annotated with its enclosing function
   when the ref engine indexes the language. Input may also be a key
   directly: when the text matches nothing literally it is searched as a
   key instead. */
#include "ref/rf.h"
#include "trim.h"
#include <ctype.h>

#define MAX_KEYS 8
#define MAX_HITS 4000

#ifdef _WIN32
#include <windows.h>
/* narrow argv arrives in the system ANSI codepage; files and rg output are
   UTF-8, so convert for matching/display. Roundtrip is lossless whenever
   the codepage can represent the input (true for CJK on CP936 systems);
   the original ACP bytes are still passed to child rg via cmd, which
   restores them to wide chars. */
char *argv_to_utf8(const char *s) {
    int w = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (w <= 0)
        return NULL;
    wchar_t *ws = malloc((size_t)w * sizeof(wchar_t));
    if (!ws)
        return NULL;
    MultiByteToWideChar(CP_ACP, 0, s, -1, ws, w);
    int u = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    char *us = u > 0 ? malloc((size_t)u) : NULL;
    if (us)
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, us, u, NULL, NULL);
    free(ws);
    return us;
}
#else
char *argv_to_utf8(const char *s) { return rf_sdup(s); }
#endif

const char *const RES_GLOBS[9] = {"*.arb",        "*.json", "*.yaml",
                                  "*.yml",        "*.xml",  "*.strings",
                                  "*.properties", "*.po",   "*.resx"};
const char *const CODE_GLOBS[12] = {"!*.arb",        "!*.json",   "!*.yaml",
                                    "!*.yml",        "!*.xml",    "!*.strings",
                                    "!*.properties", "!*.po",     "!*.resx",
                                    "!*.lock",       "!*.g.dart", "!*.gr.dart"};

typedef struct {
    char *file, *content, *key; /* key=NULL for literal hits */
    long line;
} SHit;
static SHit *s_hits;
static int s_n, s_cap;

static void s_add_hit(const char *file, long line, const char *content,
                      const char *key) {
    for (int i = 0; i < s_n; i++) /* dedup by file+line across passes */
        if (s_hits[i].line == line && !strcmp(s_hits[i].file, file)) {
            if (key && !s_hits[i].key) { /* upgrade literal hit to call site */
                free(s_hits[i].key);
                s_hits[i].key = rf_sdup(key);
            }
            return;
        }
    if (s_n == MAX_HITS)
        return;
    if (s_n == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 32;
        s_hits = realloc(s_hits, (size_t)s_cap * sizeof(SHit));
    }
    s_hits[s_n].file = rf_sdup(file);
    s_hits[s_n].content = rf_sdup(content);
    s_hits[s_n].key = key ? rf_sdup(key) : NULL;
    s_hits[s_n].line = line;
    s_n++;
}

int is_resource(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path)
        return 0;
    dot++;
    for (size_t i = 0; i < sizeof(RES_GLOBS) / sizeof(RES_GLOBS[0]); i++)
        if (!strcmp(dot, RES_GLOBS[i] + 2)) /* glob without the leading "*." */
            return 1;
    return 0;
}

/* drop trailing blanks (and a trailing line-continuation backslash) so hit
   lines print clean; content points into the caller's rg buffer */
void trim_content(const char *content) {
    char *c = (char *)content;
    size_t n = strlen(c);
    while (n > 0 && (c[n - 1] == ' ' || c[n - 1] == '\t' || c[n - 1] == '\r' ||
                     c[n - 1] == '\n'))
        c[--n] = '\0';
    if (n > 0 && c[n - 1] == '\\') {
        c[--n] = '\0';
        while (n > 0 && (c[n - 1] == ' ' || c[n - 1] == '\t'))
            c[--n] = '\0';
    }
}

/* parse one `path:line:content` rg line; colons inside the path that are
   not followed by a line number (drive letters) are skipped */
int parse_rg_line(const char *s, char *file, size_t fcap, long *line,
                  const char **content) {
    for (const char *p = s; *p; p++) {
        if (*p != ':')
            continue;
        char *end;
        long v = strtol(p + 1, &end, 10);
        if (end > p + 1 && *end == ':' && v > 0) {
            size_t fl = (size_t)(p - s);
            if (fl >= fcap)
                fl = fcap - 1;
            memcpy(file, s, fl);
            file[fl] = '\0';
            if (fl >= 2 && file[0] == '.' && file[1] == '/') {
                memmove(file, file + 2, fl - 1); /* match ast-grep paths */
                fl -= 2;
            }
            *line = v;
            *content = end + 1;
            trim_content(*content);
            return 1;
        }
    }
    return 0;
}

/* identifier bound to <needle> on the same line:
   "key": "needle" | key: needle | key=needle | name="key">needle
   value position: skips quotes/colons/equals/angle brackets backwards;
   key position: needle directly followed by ": — then the needle is the
   key itself (input-as-key case). Parentheses stop the scan so
   gettext-style _("needle") yields no key (the string is its own key);
   generic markup words are denied so <value>needle etc. produce nothing.
   Caller frees. */
char *extract_key(const char *content, const char *needle) {
    static const char *const DENY[] = {"string", "name",   "value",   "data",
                                       "msgid",  "msgstr", "msgctxt", "text",
                                       "entry",  "key"};
    size_t nl = strlen(needle);
    for (const char *p = strstr(content, needle); p;
         p = strstr(p + 1, needle)) {
        /* key position? needle directly followed by [spaces] quote [spaces] :
           e.g. "forceDarkMode": ... -> the needle itself is the key */
        const char *q = p + nl;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '"' || *q == '\'') {
            q++;
            while (*q == ' ' || *q == '\t')
                q++;
        }
        if (*q == ':')
            return rf_sdup(needle);
        /* value position: walk backwards over binder characters */
        const char *end = p;
        int skipped = 0;
        while (end > content && skipped < 32 && strchr(" \t\"':=<>", end[-1])) {
            end--;
            skipped++;
        }
        if (end == p)
            continue;
        const char *start = end;
        while (start > content &&
               (isalnum((unsigned char)start[-1]) || start[-1] == '_'))
            start--;
        size_t len = (size_t)(end - start);
        if (len < 2 || len > 126)
            continue;
        int denied = 0;
        for (size_t i = 0; i < sizeof(DENY) / sizeof(DENY[0]); i++)
            if (!memcmp(start, DENY[i], len) && DENY[i][len] == '\0')
                denied = 1;
        if (denied)
            continue;
        char *k = malloc(len + 1);
        if (!k)
            return NULL;
        memcpy(k, start, len);
        k[len] = '\0';
        return k;
    }
    return NULL;
}

/* rg with fixed strings; globs narrow (positive) or exclude (negative) */
char *run_rg(const char *pattern, const char *path, int words,
             const char *const *globs, int nglobs) {
    const char *args[40];
    int ac = 0;
    args[ac++] = "rg";
    args[ac++] = "-n";
    args[ac++] = "--color=never";
    args[ac++] = "--no-heading";
    args[ac++] = "--path-separator=/";
    args[ac++] = "--fixed-strings";
    if (words)
        args[ac++] = "-w";
    args[ac++] = "-e";
    args[ac++] = pattern;
    for (int i = 0; i < nglobs && ac < 36; i++) {
        args[ac++] = "-g";
        args[ac++] = globs[i];
    }
    args[ac++] = "--";
    args[ac++] = path;
    return run_cmd_capture(args, ac);
}

/* identifier introduced by #define on this line (e.g. #define HINT_RG "...") */
char *define_key(const char *s) {
    const char *d = strstr(s, "#define");
    if (!d)
        return NULL;
    d += 7;
    while (*d == ' ' || *d == '\t')
        d++;
    if (!isalpha((unsigned char)*d) && *d != '_')
        return NULL;
    const char *e = d;
    while (isalnum((unsigned char)*e) || *e == '_')
        e++;
    size_t len = (size_t)(e - d);
    if (len < 2 || len > 126)
        return NULL;
    char *k = malloc(len + 1);
    if (!k)
        return NULL;
    memcpy(k, d, len);
    k[len] = '\0';
    return k;
}

/* rightmost identifier run ending just before cut (after spaces); malloc'd
   or NULL */
char *ident_before(const char *b, const char *cut) {
    const char *q = cut;
    while (q > b && (q[-1] == ' ' || q[-1] == '\t'))
        q--;
    const char *end = q;
    while (q > b && (isalnum((unsigned char)q[-1]) || q[-1] == '_'))
        q--;
    if (q == end)
        return NULL;
    size_t len = (size_t)(end - q);
    if (len < 2 || len > 126)
        return NULL;
    char *k = malloc(len + 1);
    if (!k)
        return NULL;
    memcpy(k, q, len);
    k[len] = '\0';
    return k;
}

/* identifier assigned the quoted <needle> on the same line:
   IDENT = "needle" | IDENT : type = "needle" | IDENT := "needle"
   covers const/let/val/static bindings in C++, Rust, Zig, Kotlin, Swift,
   Dart, Go, JS/TS, Python. malloc'd key or NULL. */
char *bind_key(const char *s, const char *needle) {
    static const char *const DENY[] = {"case", "default", "return", "else",
                                       NULL};
    for (const char *p = strstr(s, needle); p; p = strstr(p + 1, needle)) {
        if (p == s || (p[-1] != '"' && p[-1] != '\''))
            continue; /* not a quoted literal */
        const char *e = p - 2;
        while (e >= s &&
               (isalnum((unsigned char)*e) || strchr(" \t_&:.<>[],*", *e)))
            e--;
        if (e < s || *e != '=')
            continue;
        if (e > s && e[-1] != ':' && strchr("=!<>+-*/%&|^[", e[-1]))
            continue; /* == <= != += are not assignment */
        const char *b = e;
        while (b > s && !strchr(";{}()", b[-1]))
            b--; /* statement span */
        const char *c = b;
        while (c < e && *c != ':')
            c++;
        char *k = NULL;
        if (c < e) /* typed decl: IDENT : type = needle */
            k = ident_before(b, c);
        for (int i = 0; k && DENY[i]; i++)
            if (!strcmp(k, DENY[i])) {
                free(k);
                k = NULL;
                break;
            }
        if (!k)
            k = ident_before(b, e);
        if (k)
            return k;
    }
    return NULL;
}

/* identifier bound to the hit line in code: a #define on the line itself, or
   on a continuation line above (lines joined by trailing backslash) */
char *code_key(const char *file, long line, const char *content,
               const char *needle) {
    char *k = define_key(content);
    if (k)
        return k;
    k = bind_key(content, needle);
    if (k)
        return k;
    FILE *f = fopen(file, "rb");
    if (!f)
        return NULL;
    char ring[9][1024] = {{0}};
    char buf[1024];
    long nline = 0;
    while (fgets(buf, sizeof(buf), f)) {
        nline++;
        if (nline > line - 1)
            break;
        memmove(ring, ring + 1, 8 * 1024);
        memcpy(ring[8], buf, strlen(buf) + 1);
    }
    fclose(f);
    if (nline < line - 1) { /* file changed under us */
        return NULL;
    }
    for (int i = 8; i >= 0; i--) {
        if (!ring[i][0])
            break;
        k = define_key(ring[i]);
        if (k)
            return k;
        char *r = ring[i] + strlen(ring[i]);
        while (r > ring[i] && (r[-1] == '\n' || r[-1] == '\r' || r[-1] == ' ' ||
                               r[-1] == '\t'))
            r--;
        if (r == ring[i] || r[-1] != '\\')
            break;
    }
    return NULL;
}

/* C-style identifier: starts with alpha/_, then alnum/_ */
static int is_ident(const char *s) {
    if (!isalpha((unsigned char)s[0]) && s[0] != '_')
        return 0;
    for (const char *p = s + 1; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_')
            return 0;
    return 1;
}

void cmd_string(int argc, const char *const *argv) {
    if (argc < 1 || !argv[0][0]) {
        fprintf(stderr, "error: usage: trim string <text> [path]\n");
        return;
    }
    const char *text = argv[0];
    const char *path = ".";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", argv[i]);
            return;
        }
        path = argv[i];
    }
    char *text_utf8 = argv_to_utf8(text);
    if (!text_utf8)
        text_utf8 = rf_sdup(text);
    int dbg = getenv("TRIM_STRING_DEBUG") != NULL;
    if (dbg)
        fprintf(stderr, "[sdbg] A text=%s\n", text);

    Buf out = {0};
    rf_buf_addf(&out, "[STRING v1] text=%s path=%s\n", text_utf8, path);

    /* ---- 1 LITERAL: where the string itself appears ---- */
    rf_buf_add(&out, "1 LITERAL\n");
    char keys[MAX_KEYS][128];
    int nkeys = 0, nlit = 0, ncode_lit = 0;
    char *lit = run_rg(text, path, 0, NULL, 0);
    for (char *ln = lit; ln && *ln;) {
        char *nl = strchr(ln, '\n');
        if (nl)
            *nl = '\0';
        char file[1024];
        long line;
        const char *content;
        if (parse_rg_line(ln, file, sizeof(file), &line, &content)) {
            nlit++;
            if (is_resource(file)) {
                char *k = extract_key(content, text_utf8);
                if (k && nkeys < MAX_KEYS) {
                    int dup = 0;
                    for (int i = 0; i < nkeys; i++)
                        if (!strcmp(keys[i], k))
                            dup = 1;
                    if (!dup) {
                        memcpy(keys[nkeys], k, strlen(k) + 1);
                        nkeys++;
                    }
                }
                free(k);
            } else {
                ncode_lit++;
                if (nkeys < MAX_KEYS) {
                    char *k = code_key(file, line, content, text_utf8);
                    if (k) {
                        int dup = 0;
                        for (int i = 0; i < nkeys; i++)
                            if (!strcmp(keys[i], k))
                                dup = 1;
                        if (!dup) {
                            memcpy(keys[nkeys], k, strlen(k) + 1);
                            nkeys++;
                        }
                    }
                    free(k);
                }
            }
            s_add_hit(file, line, content, NULL);
            rf_buf_addf(&out, "  %s:%ld:%s\n", file, line, content);
        }
        if (!nl)
            break;
        ln = nl + 1;
    }
    free(lit);
    if (dbg)
        fprintf(stderr, "[sdbg] B nlit=%d nkeys=%d ncode=%d\n", nlit, nkeys,
                ncode_lit);
    if (!nlit) {
        rf_buf_add(&out, "  (no literal match — treating input as a key)\n");
        strncpy(keys[0], text_utf8, sizeof(keys[0]) - 1);
        keys[0][sizeof(keys[0]) - 1] = '\0';
        nkeys = 1;
    } else if (ncode_lit && !nkeys && is_ident(text_utf8)) {
        /* the input is itself an identifier (macro / i18n key) that appears
           in code: adopt it as the key so translations + call sites resolve */
        strncpy(keys[0], text_utf8, sizeof(keys[0]) - 1);
        keys[0][sizeof(keys[0]) - 1] = '\0';
        nkeys = 1;
    } else if (ncode_lit && !nkeys) {
        rf_buf_addf(&out,
                    "  (hardcoded in code — %d of the above are call sites)\n",
                    ncode_lit);
    }

    /* ---- 2 KEY: identifier bound to the string (optional) ---- */
    rf_buf_add(&out, "2 KEY\n");
    if (!nkeys)
        rf_buf_add(&out, "  (none)\n");
    for (int i = 0; i < nkeys; i++)
        rf_buf_addf(&out, "  %s\n", keys[i]);

    /* ---- 3 TRANSLATIONS: the key in every resource file ---- */
    rf_buf_add(&out, "3 TRANSLATIONS\n");
    int nres = 0;
    for (int i = 0; i < nkeys; i++) {
        char *res = run_rg(keys[i], path, 1, RES_GLOBS,
                           (int)(sizeof(RES_GLOBS) / sizeof(RES_GLOBS[0])));
        for (char *ln = res; ln && *ln;) {
            char *nl = strchr(ln, '\n');
            if (nl)
                *nl = '\0';
            char file[1024];
            long line;
            const char *content;
            if (parse_rg_line(ln, file, sizeof(file), &line, &content)) {
                nres++;
                rf_buf_addf(&out, "  %s:%ld:%s\n", file, line, content);
            }
            if (!nl)
                break;
            ln = nl + 1;
        }
        free(res);
    }
    if (!nres)
        rf_buf_add(&out, "  (none)\n");
    if (dbg)
        fprintf(stderr, "[sdbg] C nres=%d\n", nres);

    /* ---- 4 CALL SITES: the key referenced in code ---- */
    for (int i = 0; i < nkeys; i++) {
        char *code = run_rg(keys[i], path, 1, CODE_GLOBS,
                            (int)(sizeof(CODE_GLOBS) / sizeof(CODE_GLOBS[0])));
        for (char *ln = code; ln && *ln;) {
            char *nl = strchr(ln, '\n');
            if (nl)
                *nl = '\0';
            char file[1024];
            long line;
            const char *content;
            if (parse_rg_line(ln, file, sizeof(file), &line, &content))
                s_add_hit(file, line, content, keys[i]);
            if (!nl)
                break;
            ln = nl + 1;
        }
        free(code);
    }
    rf_buf_add(&out, "4 CALL SITES\n");
    int ncall = 0;
    int need_defs = 0;
    for (int i = 0; i < s_n; i++)
        if (s_hits[i].key)
            need_defs = 1;
    if (need_defs)
        rf_scan(path); /* index defs once for enclosing-function lookup */
    if (dbg)
        fprintf(stderr, "[sdbg] D scanned ndefs=%d ncallees=%d\n", rf_ndefs,
                rf_ncallees);
    for (int i = 0; i < s_n; i++) {
        SHit *h = &s_hits[i];
        if (!h->key)
            continue;
        const char *fn = "?";
        int di = rf_def_at(h->file, h->line);
        if (di >= 0)
            fn = rf_defs[di].name;
        rf_buf_addf(&out, "  %s:%ld  %s  |  %s\n", h->file, h->line, fn,
                    h->content);
        ncall++;
    }
    if (!ncall)
        rf_buf_add(&out, "  (none)\n");
    if (dbg)
        fprintf(stderr, "[sdbg] E ncall=%d\n", ncall);

    for (int i = 0; i < s_n; i++) {
        free(s_hits[i].file);
        free(s_hits[i].content);
        free(s_hits[i].key);
    }
    free(s_hits);
    s_hits = NULL;
    s_n = s_cap = 0;
    free(text_utf8);
    rf_emit_capped(&out, "STRING");
}
