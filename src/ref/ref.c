/* ref.c -- trim ref <symbol> [path] [--depth N]
   Call-tree lookup: what <symbol> calls (CALLS OUT) and who calls it
   (CALLED BY), grouped by file + enclosing function + line, traversed to
   depth (default 2). Falls back to plain rg when ast-grep is missing or
   no language matches. */
#include "../trim.h"
#include "rf.h"
#include "rx.h"

void cmd_ref(int argc, const char *const *argv) {
    if (argc < 1 || !argv[0][0]) {
        fprintf(stderr, "error: usage: trim ref <symbol> [path] [--depth N]\n");
        return;
    }
    const char *symbol = argv[0];
    const char *path = ".";
    int depth = 2;
    int mode = RF_EXACT;
    re_t re = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            depth = atoi(argv[++i]);
        } else if (!strncmp(argv[i], "--depth=", 8)) {
            depth = atoi(argv[i] + 8);
        } else if (!strcmp(argv[i], "--sub")) {
            mode = RF_SUB;
        } else if (!strcmp(argv[i], "--re")) {
            mode = RF_RE;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    if (depth < 1)
        depth = 1;
    if (depth > 8)
        depth = 8;
    /* regex patterns are used verbatim; plain symbols reduce to their
       last path segment (a.b::c -> c) */
    const char *target = mode == RF_RE ? symbol : rf_last_segment(symbol);
    const char *modestr = mode == RF_SUB  ? "sub"
                          : mode == RF_RE ? "re"
                                          : "exact";
    if (mode == RF_RE) {
        re = re_compile(target);
        if (!re) {
            fprintf(stderr, "error: bad regex '%s'\n", target);
            return;
        }
    }
    Buf out = {0};
    rf_buf_addf(&out, "[REF v1] symbol=%s mode=%s depth=%d path=%s\n", symbol,
                modestr, depth, path);

    rf_scan(path);

    if (!rf_ncallees && !rf_ndefs) {
        rf_rg_fallback(&out, target, path);
        rf_emit_ref(&out);
        return;
    }

    /* ---- CALLS OUT: what the symbol calls, downward to depth ---- */
    rf_buf_add(&out, "CALLS OUT\n");
    int roots[50];
    int ntotal = rf_find_defs_by(target, mode, re, roots, 50);
    int nroots = ntotal < 50 ? ntotal : 50;
    if (ntotal == 0) {
        rf_buf_addf(&out, "  (no def matches %s -- %s)\n", target, modestr);
    } else {
        int dvcap = 0, ndvis = 0;
        char **dvis = NULL;
        int dfrontcap = nroots > 16 ? nroots : 16;
        int ndfront = 0;
        int *dfront = malloc(sizeof(int) * (size_t)dfrontcap);
        for (int r = 0; r < nroots; r++) {
            if (rf_visited_has(dvis, ndvis, rf_defs[roots[r]].name))
                continue; /* same-name defs: one section */
            dvis = rf_push_name(dvis, &ndvis, &dvcap, rf_defs[roots[r]].name);
            dfront[ndfront++] = roots[r];
        }
        for (int level = 1; level <= depth && ndfront; level++) {
            int *dnext = malloc(sizeof(int) * 16);
            int dn = 0, dncap = 16;
            for (int f = 0; f < ndfront; f++) {
                int di = dfront[f];
                rf_buf_addf(&out, "%s\n", rf_defs[di].name);
                for (int ci = 0; ci < rf_ncallees; ci++) {
                    Callee *c = &rf_callees[ci];
                    for (int s = 0; s < c->n; s++) {
                        Site *st = &c->sites[s];
                        if (rf_site_def(st) != di)
                            continue;
                        char line1[96];
                        rf_first_line(st->text, line1, sizeof(line1));
                        rf_buf_addf(&out, "  %s:%ld  %s  |  %s\n", st->file,
                                    st->line + 1, c->name, line1);
                        if (ndvis < 4000 &&
                            !rf_visited_has(dvis, ndvis, c->name)) {
                            int cdef = rf_find_def_by_name(c->name);
                            if (cdef >= 0) {
                                dvis =
                                    rf_push_name(dvis, &ndvis, &dvcap, c->name);
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
        if (ntotal > nroots)
            rf_buf_addf(&out, "  (%d more def matches -- narrow pattern)\n",
                        ntotal - nroots);
        for (int i = 0; i < ndvis; i++)
            free(dvis[i]);
        free(dvis);
    }

    /* ---- CALLED BY: callers of the symbol, upward to depth ---- */
    rf_buf_add(&out, "CALLED BY\n");
    int vcap = 0, nvisited = 0;
    char **visited = NULL;
    int fcap = 0, nfrontier = 0;
    char **frontier = NULL;
    int cbtotal = 0;
    for (int ci = 0; ci < rf_ncallees; ci++)
        if (rf_name_matches(rf_callees[ci].name, mode, target, re))
            cbtotal++;
    if (cbtotal == 0) {
        rf_buf_addf(&out, "  (no callee matches %s -- %s)\n", target, modestr);
    } else {
        int cbshown = 0;
        for (int ci = 0; ci < rf_ncallees && cbshown < 50; ci++) {
            if (!rf_name_matches(rf_callees[ci].name, mode, target, re))
                continue;
            visited =
                rf_push_name(visited, &nvisited, &vcap, rf_callees[ci].name);
            frontier =
                rf_push_name(frontier, &nfrontier, &fcap, rf_callees[ci].name);
            cbshown++;
        }
        for (int level = 1; level <= depth && nfrontier; level++) {
            int ncap = 0, nnext = 0;
            char **next = NULL;
            for (int f = 0; f < nfrontier; f++) {
                Callee *c = rf_find_callee(frontier[f]);
                if (!c || !c->n)
                    continue;
                rf_buf_addf(&out, "%s\n", frontier[f]);
                for (int s = 0; s < c->n; s++) {
                    Site *st = &c->sites[s];
                    int di = rf_site_def(st);
                    const char *fn = di >= 0 ? rf_defs[di].name : "(top-level)";
                    char line1[96];
                    rf_first_line(st->text, line1, sizeof(line1));
                    rf_buf_addf(&out, "  %s:%ld  %s  |  %s\n", st->file,
                                st->line + 1, fn, line1);
                    if (di >= 0 &&
                        !rf_visited_has(visited, nvisited, rf_defs[di].name) &&
                        nnext < 4000) {
                        next =
                            rf_push_name(next, &nnext, &ncap, rf_defs[di].name);
                        visited = rf_push_name(visited, &nvisited, &vcap,
                                               rf_defs[di].name);
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
        if (cbtotal > cbshown)
            rf_buf_addf(&out, "  (%d more callee matches -- narrow pattern)\n",
                        cbtotal - cbshown);
    }

    rf_emit_ref(&out);
}
