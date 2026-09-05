/* keyword.c -- trim keyword <kw...> [path] [--depth N]
   IDA-style xref exploration from fuzzy memory: keywords -> matching
   symbols + string-bound identifiers (ranked by distinct keywords hit) ->
   every textual reference with its enclosing function -> callers walked
   up the call graph to depth N. Composes the string.c binding resolvers
   with the ref engine index; output capped like ref/string. */
#include "ref/rf.h"
#include "trim.h"
#include <ctype.h>
#include <sys/stat.h>

#define KW_MAX 12   /* max keywords */
#define SEED_MAX 20 /* seeds shown */
#define SITE_MAX 50 /* reference sites per seed */

static int path_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

/* distinct keywords matched by name (smartcase substring) */
static int kw_score(const char *name, const char *const *kws, int nkw) {
    int score = 0;
    for (int i = 0; i < nkw; i++)
        if (rf_name_matches(name, RF_SUB, kws[i], NULL))
            score++;
    return score;
}

typedef struct {
    char *name;
    int score;
} Seed;

static int cmp_seed(const void *a, const void *b) {
    const Seed *x = a, *y = b;
    if (x->score != y->score)
        return y->score - x->score;
    return strcmp(x->name, y->name);
}

void cmd_keyword(int argc, const char *const *argv) {
    if (argc < 1) {
        fprintf(stderr,
                "error: usage: trim keyword <kw...> [path] [--depth N]\n");
        return;
    }
    const char *path = ".";
    int depth = 2;
    int lastpos = -1;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            depth = atoi(argv[++i]);
        } else if (!strncmp(argv[i], "--depth=", 8)) {
            depth = atoi(argv[i] + 8);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", argv[i]);
            return;
        } else {
            lastpos = i;
        }
    }
    if (depth < 1)
        depth = 1;
    if (depth > 8)
        depth = 8;
    if (lastpos >= 0 && path_exists(argv[lastpos]))
        path = argv[lastpos]; /* the last positional that exists on disk */
    else
        lastpos = -1;

    const char *kws[KW_MAX];
    char *utf8[KW_MAX];
    int nkw = 0;
    for (int i = 0; i < argc && nkw < KW_MAX; i++) {
        if (i == lastpos)
            continue;
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            i++;
            continue;
        }
        if (!strncmp(argv[i], "--depth=", 8))
            continue;
        if (argv[i][0] == '-')
            continue;
        utf8[nkw] = argv_to_utf8(argv[i]);
        kws[nkw] = utf8[nkw] ? utf8[nkw] : argv[i];
        nkw++;
    }
    if (nkw == 0) {
        fprintf(stderr,
                "error: usage: trim keyword <kw...> [path] [--depth N]\n");
        return;
    }

    Buf out = {0};
    rf_buf_add(&out, "[KEYWORD v1] kws=");
    for (int i = 0; i < nkw; i++)
        rf_buf_addf(&out, "%s%s", i ? "," : "", kws[i]);
    rf_buf_addf(&out, " path=%s depth=%d\n", path, depth);

    rf_scan(path); /* one index pass; the rest is in-memory or rg */

    /* ---- SEEDS: symbols + string-bound identifiers, ranked ---- */
    Seed *sd = NULL;
    int nsd = 0, sdcap = 0;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass == 0 ? rf_ndefs : rf_ncallees;
        for (int i = 0; i < n; i++) {
            const char *name = pass == 0 ? rf_defs[i].name : rf_callees[i].name;
            int sc = kw_score(name, kws, nkw);
            if (!sc)
                continue;
            int dup = 0;
            for (int j = 0; j < nsd; j++)
                if (!strcmp(sd[j].name, name)) {
                    dup = 1;
                    break;
                }
            if (dup)
                continue;
            if (nsd == sdcap) {
                sdcap = sdcap ? sdcap * 2 : 32;
                sd = realloc(sd, (size_t)sdcap * sizeof(Seed));
            }
            sd[nsd].name = rf_sdup(name);
            sd[nsd].score = sc;
            nsd++;
        }
    }
    /* string literals / i18n keys containing any keyword */
    const char *args[10 + 2 * KW_MAX];
    int ac = 0;
    args[ac++] = "rg";
    args[ac++] = "-n";
    args[ac++] = "-i";
    args[ac++] = "--color=never";
    args[ac++] = "--no-heading";
    args[ac++] = "--path-separator=/";
    args[ac++] = "--fixed-strings";
    for (int i = 0; i < nkw; i++) {
        args[ac++] = "-e";
        args[ac++] = kws[i];
    }
    args[ac++] = "--";
    args[ac++] = path;
    char *raw = run_cmd_capture(args, ac);
    for (char *ln = raw; ln && *ln;) {
        char *nl = strchr(ln, '\n');
        if (nl)
            *nl = '\0';
        char file[1024];
        long line;
        const char *content;
        if (parse_rg_line(ln, file, sizeof(file), &line, &content)) {
            char *k = NULL;
            for (int i = 0; i < nkw && !k; i++) {
                if (!strstr(content, kws[i]))
                    continue;
                if (is_resource(file))
                    k = extract_key(content, kws[i]);
                else
                    k = code_key(file, line, content, kws[i]);
            }
            if (k) {
                int dup = 0;
                for (int j = 0; j < nsd; j++)
                    if (!strcmp(sd[j].name, k)) {
                        dup = 1;
                        break;
                    }
                if (!dup) {
                    if (nsd == sdcap) {
                        sdcap = sdcap ? sdcap * 2 : 32;
                        sd = realloc(sd, (size_t)sdcap * sizeof(Seed));
                    }
                    sd[nsd].name = k; /* adopted */
                    sd[nsd].score = kw_score(k, kws, nkw);
                    nsd++;
                } else
                    free(k);
            }
        }
        if (!nl)
            break;
        ln = nl + 1;
    }
    free(raw);

    qsort(sd, (size_t)nsd, sizeof(Seed), cmp_seed);
    rf_buf_add(&out, "SEEDS\n");
    if (!nsd)
        rf_buf_add(&out, "  (no symbols or string-bound identifiers match)\n");
    int nshown = nsd < SEED_MAX ? nsd : SEED_MAX;
    for (int i = 0; i < nshown; i++) {
        int di = rf_find_def_by_name(sd[i].name);
        if (di >= 0)
            rf_buf_addf(&out, "  %s  (%d/%d kws)  %s:%ld\n", sd[i].name,
                        sd[i].score, nkw, rf_defs[di].file,
                        rf_defs[di].start + 1);
        else
            rf_buf_addf(&out, "  %s  (%d/%d kws)\n", sd[i].name, sd[i].score,
                        nkw);
    }
    if (nsd > nshown)
        rf_buf_addf(&out, "  (%d more -- narrow keywords)\n", nsd - nshown);

    /* ---- REFERENCES: every textual site, with enclosing function ---- */
    rf_buf_add(&out, "REFERENCES\n");
    for (int i = 0; i < nshown; i++) {
        char *code = run_rg(sd[i].name, path, 1, CODE_GLOBS,
                            (int)(sizeof(CODE_GLOBS) / sizeof(CODE_GLOBS[0])));
        int total = 0;
        for (char *ln = code; ln && *ln;) {
            char *nl = strchr(ln, '\n');
            if (nl)
                *nl = '\0';
            char file[1024];
            long line;
            const char *content;
            if (parse_rg_line(ln, file, sizeof(file), &line, &content)) {
                total++;
                if (total == 1)
                    rf_buf_addf(&out, "  %s\n", sd[i].name);
                if (total <= SITE_MAX) {
                    const char *fn = "?";
                    int di = rf_def_at(file, line);
                    if (di >= 0)
                        fn = rf_defs[di].name;
                    rf_buf_addf(&out, "  %s:%ld  %s  |  %s\n", file, line, fn,
                                content);
                }
            }
            if (!nl)
                break;
            ln = nl + 1;
        }
        if (total > SITE_MAX)
            rf_buf_addf(&out, "  (%d more sites -- narrow keywords)\n",
                        total - SITE_MAX);
        free(code);
    }

    /* ---- CALLERS: BFS up the call graph, depth N (ref.c CALLED BY) ---- */
    rf_buf_add(&out, "CALLERS\n");
    int vcap = 0, nvisited = 0;
    char **visited = NULL;
    int fcap = 0, nfrontier = 0;
    char **frontier = NULL;
    for (int i = 0; i < nshown; i++) {
        if (!rf_find_callee(sd[i].name))
            continue;
        if (rf_visited_has(visited, nvisited, sd[i].name))
            continue;
        visited = rf_push_name(visited, &nvisited, &vcap, sd[i].name);
        frontier = rf_push_name(frontier, &nfrontier, &fcap, sd[i].name);
    }
    if (!nfrontier)
        rf_buf_add(&out, "  (none)\n");
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
                    next = rf_push_name(next, &nnext, &ncap, rf_defs[di].name);
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
    for (int i = 0; i < nfrontier; i++)
        free(frontier[i]);
    free(frontier);
    for (int i = 0; i < nvisited; i++)
        free(visited[i]);
    free(visited);
    (void)fcap;

    for (int i = 0; i < nsd; i++)
        free(sd[i].name);
    free(sd);
    for (int i = 0; i < nkw; i++)
        free(utf8[i]);
    rf_emit_capped(&out, "KEYWORD");
}
