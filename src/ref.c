/* ref.c — trim ref <symbol> [path] [--depth N]
   Call-tree lookup: find every function that calls <symbol>, grouped by
   file + enclosing function + line, then callers of those callers up to
   depth (default 2).

   Engine: one `ast-grep run --kind <callKind> -l <lang>` pass per language
   builds a callee -> call-sites index; `--kind <defKind>` passes locate
   enclosing functions. Falls back to plain rg when ast-grep is missing or
   no language matches. Kinds verified against ast-grep 0.45 for every
   profiled language. Limitations: syntax-level (no type resolution);
   same-named functions merge. */

#include "trim.h"
#include <ctype.h>
#include <stdarg.h>

typedef struct {
    const char *lang;
    const char *exts;     /* comma-separated, no dot */
    const char *callKind; /* kind whose text is a call */
    const char *defKinds; /* comma-separated kinds bearing bodies */
} RefProfile;

static const RefProfile PROFILES[] = {
    {"dart", "dart", "call_expression",
     "function_declaration,method_declaration"},
    {"javascript", "js,mjs,cjs", "call_expression",
     "function_declaration,method_definition"},
    {"typescript", "ts", "call_expression",
     "function_declaration,method_definition"},
    {"tsx", "tsx", "call_expression", "function_declaration,method_definition"},
    {"python", "py", "call", "function_definition"},
    {"go", "go", "call_expression", "function_declaration,method_declaration"},
    {"rust", "rs", "call_expression", "function_item"},
    {"java", "java", "method_invocation",
     "method_declaration,constructor_declaration"},
    {"c", "c,h", "call_expression", "function_definition"},
    {"cpp", "cc,cpp,cxx,hpp,hh", "call_expression", "function_definition"},
    {"csharp", "cs", "invocation_expression",
     "method_declaration,constructor_declaration"},
    {"kotlin", "kt,kts", "call_expression", "function_declaration"},
    {"ruby", "rb", "call", "method"},
    {"php", "php", "function_call_expression",
     "function_definition,method_declaration"},
    {"swift", "swift", "call_expression", "function_declaration"},
    {"lua", "lua", "function_call", "function_declaration,function_definition"},
    {"bash", "sh,bash", "command", "function_definition"},
    {"elixir", "ex,exs", "call", "call"},
};
#define NPROFILES (sizeof(PROFILES) / sizeof(PROFILES[0]))

/* ---------- small helpers ---------- */

static char *s_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------- growable output buffer ---------- */

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static void buf_addn(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8192;
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}
static void buf_add(Buf *b, const char *s) { buf_addn(b, s, strlen(s)); }
static void buf_addf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    buf_add(b, tmp);
}

/* ---------- json (ast-grep --json=compact) field extraction ---------- */

/* copy JSON string starting after opening quote, unescaping \" \\ \/ \n \r
   \t \b \f and \uXXXX (basic plane); returns malloc'd C string */
static char *json_unescape(const char *p, const char **end) {
    size_t cap = 64, n = 0;
    char *out = malloc(cap);
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'u': {
                unsigned v = 0;
                int i;
                for (i = 1; i <= 4 && isxdigit((unsigned char)p[i]); i++) {
                    char h = p[i];
                    v = v * 16 +
                        (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                p += i - 1;
                if (n + 4 > cap) {
                    cap *= 2;
                    out = realloc(out, cap);
                }
                if (v < 0x80) {
                    c = (char)v;
                } else if (v < 0x800) {
                    out[n++] = (char)(0xC0 | (v >> 6));
                    c = (char)(0x80 | (v & 0x3F));
                } else {
                    out[n++] = (char)(0xE0 | (v >> 12));
                    out[n++] = (char)(0x80 | ((v >> 6) & 0x3F));
                    c = (char)(0x80 | (v & 0x3F));
                }
                break;
            }
            default:
                c = *p;
                break;
            }
        }
        if (n + 2 > cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[n++] = c;
        p++;
    }
    out[n] = '\0';
    if (end)
        *end = *p == '"' ? p + 1 : p;
    return out;
}

static const char *find_key(const char *lo, const char *hi, const char *key) {
    size_t kl = strlen(key);
    for (const char *p = lo; p + kl < hi; p++)
        if (!strncmp(p, key, kl))
            return p + kl;
    return NULL;
}

/* one match object: text, start/end line, file */
typedef struct {
    char *text, *file;
    long start, end;
} GrepHit;

/* parse matches array; objects start with {"text":" (9 chars) and the
   fields we need appear before the optional "metaVariables" member */
static GrepHit *parse_hits(const char *json, int *out_n) {
    static const char OBJ[] = "{\"text\":\"";
    GrepHit *hits = NULL;
    int n = 0, cap = 0;
    const char *p = json;
    while ((p = strstr(p, OBJ)) != NULL) {
        const char *obj = p;
        p += sizeof(OBJ) - 1;
        const char *nobj = strstr(p, "{\"text\":\"");
        const char *meta = strstr(p, "\"metaVariables\"");
        const char *objEnd = nobj;
        if (meta && (!objEnd || meta < objEnd))
            objEnd = meta;
        if (!objEnd)
            objEnd = p + strlen(p);
        GrepHit h = {0};
        h.text = json_unescape(p, NULL);
        const char *k = find_key(obj, objEnd, "\"start\":{\"line\":");
        if (k)
            h.start = strtol(k, NULL, 10);
        k = find_key(obj, objEnd, "\"end\":{\"line\":");
        if (k)
            h.end = strtol(k, NULL, 10);
        k = find_key(obj, objEnd, "\"file\":\"");
        if (k)
            h.file = json_unescape(k, NULL);
        if (h.file && h.text) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                hits = realloc(hits, (size_t)cap * sizeof(*hits));
            }
            hits[n++] = h;
        } else {
            free(h.text);
            free(h.file);
        }
        p = objEnd;
    }
    *out_n = n;
    return hits;
}

/* ---------- name extraction ---------- */

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

/* callee/def name = identifier token before first '('; keyword-aware for
   go/swift/kotlin receivers; returns last segment after '.'/'::'/'->'.
   returns NULL when no usable name (ruby parenless calls etc.) */
static char *name_from_text(const char *text) {
    const char *lp = strchr(text, '(');
    if (!lp)
        return NULL;
    return last_segment_before(text, lp);
}

/* callee of a call node: its args-open paren is the '(' whose matching ')'
   is the LAST ')' in the node text (node text ends at the args close) */
static char *callee_from_call(const char *text) {
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
static const char *last_segment(const char *sym) {
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

/* ---------- indexes ---------- */

typedef struct {
    char *file, *text;
    long line;
    int defidx; /* cached enclosing def, -1 until resolved */
} Site;
typedef struct {
    char *name;
    Site *sites;
    int n, cap;
} Callee;
typedef struct {
    char *file, *name;
    long start, end;
} Def;

static Callee *g_callees;
static int g_ncallees, g_ccallees;
static Def *g_defs;
static int g_ndefs, g_cdefs;

static void add_site(const char *name, const char *file, long line,
                     const char *text) {
    Callee *c = NULL;
    for (int i = 0; i < g_ncallees; i++)
        if (!strcmp(g_callees[i].name, name)) {
            c = &g_callees[i];
            break;
        }
    if (!c) {
        if (g_ncallees == g_ccallees) {
            g_ccallees = g_ccallees ? g_ccallees * 2 : 32;
            g_callees = realloc(g_callees, (size_t)g_ccallees * sizeof(Callee));
        }
        c = &g_callees[g_ncallees++];
        c->name = s_dup(name);
        c->cap = 0;
        c->sites = NULL;
        c->n = 0;
    }
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->sites = realloc(c->sites, (size_t)c->cap * sizeof(Site));
    }
    for (int i = 0; i < c->n; i++) /* dedup identical nodes */
        if (c->sites[i].line == line && !strcmp(c->sites[i].file, file))
            return;
    c->sites[c->n].file = s_dup(file);
    c->sites[c->n].text = s_dup(text);
    c->sites[c->n].line = line;
    c->sites[c->n].defidx = -1;
    c->n++;
}

static void add_def(const char *file, const char *name, long start, long end) {
    if (g_ndefs == g_cdefs) {
        g_cdefs = g_cdefs ? g_cdefs * 2 : 64;
        g_defs = realloc(g_defs, (size_t)g_cdefs * sizeof(Def));
    }
    g_defs[g_ndefs].file = s_dup(file);
    g_defs[g_ndefs].name = s_dup(name);
    g_defs[g_ndefs].start = start;
    g_defs[g_ndefs].end = end;
    g_ndefs++;
}

static Callee *find_callee(const char *name) {
    for (int i = 0; i < g_ncallees; i++)
        if (!strcmp(g_callees[i].name, name))
            return &g_callees[i];
    return NULL;
}

/* smallest def in file containing line; -1 when none */
static int enclosing_def(const char *file, long line) {
    int best = -1;
    for (int i = 0; i < g_ndefs; i++) {
        Def *d = &g_defs[i];
        if (strcmp(d->file, file))
            continue;
        if (line < d->start || line > d->end)
            continue;
        if (best < 0 ||
            (d->end - d->start) < (g_defs[best].end - g_defs[best].start))
            best = i;
    }
    return best;
}

/* cached def index for a call site */
static int site_def(Site *st) {
    if (st->defidx < 0)
        st->defidx = enclosing_def(st->file, st->line + 1); /* JSON 0-based */
    return st->defidx;
}

static int find_def_by_name(const char *name) {
    for (int i = 0; i < g_ndefs; i++)
        if (!strcmp(g_defs[i].name, name))
            return i;
    return -1;
}

/* ---------- ast-grep runs ---------- */

/* kind search; collect call sites (want_defs=0) or enclosing defs (1);
   returns hit count (0 = kind produced nothing) */
static int run_kind(const char *kind, const char *lang, const char *path,
                    int want_defs) {
    const char *args[12];
    int argc = 0;
    args[argc++] = "ast-grep";
    args[argc++] = "run";
    args[argc++] = "--kind";
    args[argc++] = kind;
    args[argc++] = "-l";
    args[argc++] = lang;
    args[argc++] = "--json=compact";
    args[argc++] = "--";
    args[argc++] = path;
    char *json = run_cmd_capture_raw(args, argc);
    if (getenv("TRIM_REF_DEBUG") && json)
        fprintf(stderr, "[ref-debug] kind=%s lang=%s len=%zu head=%.120s\n",
                kind, lang, strlen(json), json);
    if (!json)
        return 0;
    int n = 0;
    GrepHit *hits = parse_hits(json, &n);
    for (int i = 0; i < n; i++) {
        GrepHit *h = &hits[i];
        for (char *t = h->file; *t; t++)
            if (*t == '\\')
                *t = '/'; /* normalize sg's mixed separators */
        char *name =
            want_defs ? name_from_text(h->text) : callee_from_call(h->text);
        if (name) {
            if (want_defs)
                add_def(h->file, name, h->start,
                        h->end > 0 ? h->end : h->start);
            else
                add_site(name, h->file, h->start, h->text);
        }
        free(name);
        free(h->text);
        free(h->file);
    }
    free(hits);
    free(json);
    return n;
}

static int visited_has(char **visited, int n, const char *s) {
    for (int i = 0; i < n; i++)
        if (!strcmp(visited[i], s))
            return 1;
    return 0;
}

static char **push_name(char **arr, int *n, int *cap, const char *s) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        arr = realloc(arr, (size_t)*cap * sizeof(char *));
    }
    arr[(*n)++] = s_dup(s);
    return arr;
}

/* first line of text, trimmed of leading space; tabs/CR folded to space */
static void first_line(const char *text, char *out, size_t cap) {
    const char *nl = strchr(text, '\n');
    size_t l1 = nl ? (size_t)(nl - text) : strlen(text);
    if (l1 >= cap)
        l1 = cap - 1;
    memcpy(out, text, l1);
    out[l1] = '\0';
    for (char *t = out; *t; t++)
        if (*t == '\r' || *t == '\t')
            *t = ' ';
}

/* emit through the shared lossless compaction, like context/read */
static void emit_ref(Buf *b) {
    if (!b->data)
        return;
    size_t compact_n = b->len;
    char *compact = HUMAN ? NULL : compact_paths(b->data, &compact_n);
    if (!compact)
        compact = b->data;
    if (compact_n <= MAX_CHARS) {
        fwrite(compact, 1, compact_n, stdout);
        if (compact_n && compact[compact_n - 1] != '\n')
            fputc('\n', stdout);
    } else {
        size_t cut = MAX_CHARS;
        while (cut > 0 && compact[cut - 1] != '\n')
            cut--;
        if (cut == 0)
            cut = MAX_CHARS;
        fwrite(compact, 1, cut, stdout);
        printf("[REF_TRUNCATED:%zu/%zuc] narrow path or raise TRIM_MAX_CHARS\n",
               cut, compact_n);
    }
    if (compact != b->data)
        free(compact);
    free(b->data);
    b->data = NULL;
}

void cmd_ref(int argc, const char *const *argv) {
    if (argc < 1 || !argv[0][0]) {
        fprintf(stderr, "error: usage: trim ref <symbol> [path] [--depth N]\n");
        return;
    }
    const char *symbol = argv[0];
    const char *path = ".";
    int depth = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            depth = atoi(argv[++i]);
        } else if (!strncmp(argv[i], "--depth=", 8)) {
            depth = atoi(argv[i] + 8);
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    if (depth < 1)
        depth = 1;
    if (depth > 8)
        depth = 8;
    const char *target = last_segment(symbol);

    Buf out = {0};
    buf_addf(&out, "[REF v1] symbol=%s depth=%d path=%s\n", symbol, depth,
             path);

    /* indexes: one call pass + def passes per language profile */
    for (size_t pi = 0; pi < NPROFILES; pi++) {
        const RefProfile *pf = &PROFILES[pi];
        if (!run_kind(pf->callKind, pf->lang, path, 0))
            continue; /* language absent */
        const char *dk = pf->defKinds;
        while (*dk) {
            const char *comma = strchr(dk, ',');
            size_t kl = comma ? (size_t)(comma - dk) : strlen(dk);
            char kind[64];
            if (kl >= sizeof(kind))
                kl = sizeof(kind) - 1;
            memcpy(kind, dk, kl);
            kind[kl] = '\0';
            run_kind(kind, pf->lang, path, 1);
            dk = comma ? comma + 1 : dk + strlen(dk);
        }
    }

    if (!g_ncallees && !g_ndefs) {
        buf_add(&out, "ast-grep indexes empty — rg fallback (text-level):\n");
        const char *args[10];
        int ac = 0;
        args[ac++] = "rg";
        args[ac++] = "-n";
        args[ac++] = "--color=never";
        args[ac++] = "--no-heading";
        args[ac++] = "--fixed-strings";
        args[ac++] = target;
        args[ac++] = "--";
        args[ac++] = path;
        char *raw = run_cmd_capture_raw(args, ac);
        if (raw)
            buf_add(&out, raw);
        else
            buf_add(&out, "no matches\n");
        free(raw);
        emit_ref(&out);
        return;
    }

    /* ---- CALLS OUT: what the symbol calls, downward to depth ---- */
    buf_add(&out, "CALLS OUT\n");
    int startDef = find_def_by_name(target);
    if (startDef < 0) {
        buf_add(&out, "  (no def in scope — nothing to expand downward)\n");
    } else {
        int dvcap = 0, ndvis = 0;
        char **dvis = NULL;
        int ndfront = 0;
        int *dfront = NULL;
        dvis = push_name(dvis, &ndvis, &dvcap, g_defs[startDef].name);
        dfront = malloc(sizeof(int) * 16);
        dfront[ndfront++] = startDef;
        for (int level = 1; level <= depth && ndfront; level++) {
            int *dnext = malloc(sizeof(int) * 16);
            int dn = 0, dncap = 16;
            for (int f = 0; f < ndfront; f++) {
                int di = dfront[f];
                buf_addf(&out, "%s\n", g_defs[di].name);
                for (int ci = 0; ci < g_ncallees; ci++) {
                    Callee *c = &g_callees[ci];
                    for (int s = 0; s < c->n; s++) {
                        Site *st = &c->sites[s];
                        if (site_def(st) != di)
                            continue;
                        char line1[96];
                        first_line(st->text, line1, sizeof(line1));
                        buf_addf(&out, "  %s:%ld  %s  |  %s\n", st->file,
                                 st->line + 1, c->name, line1);
                        if (ndvis < 4000 &&
                            !visited_has(dvis, ndvis, c->name)) {
                            int cdef = find_def_by_name(c->name);
                            if (cdef >= 0) {
                                dvis = push_name(dvis, &ndvis, &dvcap, c->name);
                                if (dn == dncap) {
                                    dncap *= 2;
                                    dnext = realloc(dnext, (size_t)dncap *
                                                               sizeof(int));
                                }
                                dnext[dn++] = cdef;
                            }
                        }
                    }
                }
            }
            free(dfront);
            dfront = dnext;
            ndfront = dn;
        }
        free(dfront);
        for (int i = 0; i < ndvis; i++)
            free(dvis[i]);
        free(dvis);
    }

    /* ---- CALLED BY: callers of the symbol, upward to depth ---- */
    buf_add(&out, "CALLED BY\n");
    int vcap = 0, nvisited = 0;
    char **visited = NULL;
    int fcap = 0, nfrontier = 0;
    char **frontier = NULL;
    visited = push_name(visited, &nvisited, &vcap, target);
    frontier = push_name(frontier, &nfrontier, &fcap, target);

    for (int level = 1; level <= depth && nfrontier; level++) {
        int ncap = 0, nnext = 0;
        char **next = NULL;
        for (int f = 0; f < nfrontier; f++) {
            Callee *c = find_callee(frontier[f]);
            if (!c || !c->n)
                continue;
            buf_addf(&out, "%s\n", frontier[f]);
            for (int s = 0; s < c->n; s++) {
                Site *st = &c->sites[s];
                int di = site_def(st);
                const char *fn = di >= 0 ? g_defs[di].name : "(top-level)";
                char line1[96];
                first_line(st->text, line1, sizeof(line1));
                buf_addf(&out, "  %s:%ld  %s  |  %s\n", st->file, st->line + 1,
                         fn, line1);
                if (di >= 0 &&
                    !visited_has(visited, nvisited, g_defs[di].name) &&
                    nnext < 4000) {
                    next = push_name(next, &nnext, &ncap, g_defs[di].name);
                    visited =
                        push_name(visited, &nvisited, &vcap, g_defs[di].name);
                }
            }
        }
        for (int i = 0; i < nfrontier; i++)
            free(frontier[i]);
        free(frontier);
        frontier = next;
        nfrontier = nnext;
        fcap = ncap;
    }

    emit_ref(&out);
}
