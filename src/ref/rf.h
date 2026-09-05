/* rf.h — shared internals for trim ref (the files in src/ref/).
   Syntax-level call-tree engine over `ast-grep run --kind` passes.
   Limitations: no type resolution; same-named functions merge. */
#ifndef REF_RF_H
#define REF_RF_H

#include <stddef.h>

/* language profile: one ast-grep pass set per language */
typedef struct {
    const char *lang;
    const char *exts;     /* comma-separated, no dot */
    const char *callKind; /* kind whose text is a call */
    const char *defKinds; /* comma-separated kinds bearing bodies */
} RefProfile;

/* one match object: text, start/end line, file */
typedef struct {
    char *text, *file;
    long start, end;
} GrepHit;

/* call-site / definition index */
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

/* growable output buffer */
typedef struct {
    char *data;
    size_t len, cap;
} Buf;

/* parse.c — string/buffer helpers + ast-grep --json=compact parsing */
char *rf_sdup(const char *s);
void rf_buf_addn(Buf *b, const char *s, size_t n);
void rf_buf_add(Buf *b, const char *s);
void rf_buf_addf(Buf *b, const char *fmt, ...);
GrepHit *rf_parse_hits(const char *json, int *out_n);

/* names.c — identifier extraction from node text */
char *rf_name_from_text(const char *text);
char *rf_callee_from_call(const char *text);
const char *rf_last_segment(const char *sym);

/* index.c — callee -> sites / def tables */
extern Callee *rf_callees;
extern int rf_ncallees;
extern Def *rf_defs;
extern int rf_ndefs;
void rf_add_site(const char *name, const char *file, long line,
                 const char *text);
void rf_add_def(const char *file, const char *name, long start, long end);
Callee *rf_find_callee(const char *name);
int rf_site_def(Site *st);
int rf_find_def_by_name(const char *name);

/* engine.c — ast-grep runs, small helpers, output */
int rf_run_kind(const char *kind, const char *lang, const char *path,
                int want_defs);
int rf_visited_has(char **visited, int n, const char *s);
char **rf_push_name(char **arr, int *n, int *cap, const char *s);
void rf_first_line(const char *text, char *out, size_t cap);
void rf_emit_ref(Buf *b);

/* profile.c — index one path across all language profiles */
void rf_scan(const char *path);

#endif
