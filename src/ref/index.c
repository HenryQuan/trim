/* index.c — callee -> call-sites and definition tables */
#include <ctype.h>

#include "../trim.h"
#include "rf.h"
#include "rx.h"

Callee *rf_callees;
int rf_ncallees, rf_ccallees;
Def *rf_defs;
int rf_ndefs, rf_cdefs;

void rf_add_site(const char *name, const char *file, long line,
                 const char *text) {
    Callee *c = NULL;
    for (int i = 0; i < rf_ncallees; i++)
        if (!strcmp(rf_callees[i].name, name)) {
            c = &rf_callees[i];
            break;
        }
    if (!c) {
        if (rf_ncallees == rf_ccallees) {
            rf_ccallees = rf_ccallees ? rf_ccallees * 2 : 32;
            rf_callees =
                realloc(rf_callees, (size_t)rf_ccallees * sizeof(Callee));
        }
        c = &rf_callees[rf_ncallees++];
        c->name = rf_sdup(name);
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
    c->sites[c->n].file = rf_sdup(file);
    c->sites[c->n].text = rf_sdup(text);
    c->sites[c->n].line = line;
    c->sites[c->n].defidx = -1;
    c->n++;
}

void rf_add_def(const char *file, const char *name, long start, long end) {
    if (rf_ndefs == rf_cdefs) {
        rf_cdefs = rf_cdefs ? rf_cdefs * 2 : 64;
        rf_defs = realloc(rf_defs, (size_t)rf_cdefs * sizeof(Def));
    }
    rf_defs[rf_ndefs].file = rf_sdup(file);
    rf_defs[rf_ndefs].name = rf_sdup(name);
    rf_defs[rf_ndefs].start = start;
    rf_defs[rf_ndefs].end = end;
    rf_ndefs++;
}

Callee *rf_find_callee(const char *name) {
    for (int i = 0; i < rf_ncallees; i++)
        if (!strcmp(rf_callees[i].name, name))
            return &rf_callees[i];
    return NULL;
}

/* smallest def in file containing line; -1 when none */
static int enclosing_def(const char *file, long line) {
    int best = -1;
    for (int i = 0; i < rf_ndefs; i++) {
        Def *d = &rf_defs[i];
        if (strcmp(d->file, file))
            continue;
        if (line < d->start || line > d->end)
            continue;
        if (best < 0 ||
            (d->end - d->start) < (rf_defs[best].end - rf_defs[best].start))
            best = i;
    }
    return best;
}

/* cached def index for a call site */
int rf_site_def(Site *st) {
    if (st->defidx < 0)
        st->defidx = enclosing_def(st->file, st->line + 1); /* JSON 0-based */
    return st->defidx;
}

/* public: smallest def in file containing the 1-based line; -1 when none */
int rf_def_at(const char *file, long line) { return enclosing_def(file, line); }

int rf_find_def_by_name(const char *name) {
    for (int i = 0; i < rf_ndefs; i++)
        if (!strcmp(rf_defs[i].name, name))
            return i;
    return -1;
}

/* mode: exact | substring (smartcase — all-lowercase pat ignores case) |
   tiny regex (re = compiled re_t from rx.h) */
int rf_name_matches(const char *name, int mode, const char *pat,
                    const void *re) {
    int ml;
    if (mode == RF_RE)
        return re_matchp((re_t)re, name, &ml) >= 0;
    if (mode == RF_SUB) {
        int ci = 1;
        for (const char *p = pat; *p; p++)
            if (isupper((unsigned char)*p))
                ci = 0;
        if (!ci)
            return strstr(name, pat) != NULL;
        for (const char *h = name; *h; h++) {
            const char *a = h, *b = pat;
            while (*b &&
                   tolower((unsigned char)*a) == tolower((unsigned char)*b))
                a++, b++;
            if (!*b)
                return 1;
        }
        return 0;
    }
    return strcmp(name, pat) == 0;
}

/* def indices whose name matches; fills out[] up to max, returns total */
int rf_find_defs_by(const char *pat, int mode, const void *re, int *out,
                    int max) {
    int n = 0;
    for (int i = 0; i < rf_ndefs; i++) {
        if (!rf_name_matches(rf_defs[i].name, mode, pat, re))
            continue;
        if (n < max)
            out[n] = i;
        n++;
    }
    return n;
}
