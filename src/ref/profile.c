/* profile.c -- language profiles and the per-language index pass.
   Kinds verified against ast-grep 0.45 for every profiled language. */
#include "../trim.h"
#include "rf.h"

static const RefProfile PROFILES[] = {
    {"dart", "dart", "call_expression",
     "function_declaration,method_declaration"},
    {"javascript", "js,jsx,mjs,cjs", "call_expression",
     "function_declaration,method_definition"},
    {"typescript", "ts,mts,cts", "call_expression",
     "function_declaration,method_definition"},
    {"tsx", "tsx", "call_expression", "function_declaration,method_definition"},
    {"python", "py", "call", "function_definition"},
    {"go", "go", "call_expression", "function_declaration,method_declaration"},
    {"rust", "rs", "call_expression", "function_item"},
    {"java", "java", "method_invocation",
     "method_declaration,constructor_declaration"},
    {"c", "c,h", "call_expression", "function_definition"},
    {"cpp", "cc,cpp,cxx,hpp,hh,hxx,C,H", "call_expression",
     "function_definition"},
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

static int profile_matches(const RefProfile *pf, const char *file) {
    const char *ext = strrchr(file, '.');
    if (!ext)
        return 0;
    ext++;
    size_t len = strlen(ext);
    for (const char *p = pf->exts; *p;) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !strncmp(p, ext, n))
            return 1;
        p += n;
        if (*p)
            p++;
    }
    return 0;
}

static void scan_defs(const RefProfile *pf, const char *const *paths,
                      int npaths) {
    const char *dk = pf->defKinds;
    while (*dk) {
        const char *comma = strchr(dk, ',');
        size_t kl = comma ? (size_t)(comma - dk) : strlen(dk);
        char kind[64];
        if (kl >= sizeof(kind))
            kl = sizeof(kind) - 1;
        memcpy(kind, dk, kl);
        kind[kl] = '\0';
        rf_run_kind_paths(kind, pf->lang, paths, npaths, 1);
        dk = comma ? comma + 1 : dk + strlen(dk);
    }
}

/* Enumerate once instead of starting a tree walk for every language.
   If discovery is unavailable, retain the full-scan fallback. */
void rf_scan(const char *path) {
    const char *args[] = {"rg", "--files", "--no-config", "--", path};
    char *files = run_cmd_capture(args, 5);
    int present[NPROFILES] = {0};
    int known = 0;
    for (char *line = files; line && *line;) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        for (size_t i = 0; i < NPROFILES; i++)
            if (profile_matches(&PROFILES[i], line)) {
                present[i] = 1;
                known = 1;
            }
        if (!end)
            break;
        line = end + 1;
    }
    free(files);
    for (size_t i = 0; i < NPROFILES; i++) {
        if (known && !present[i])
            continue;
        const RefProfile *pf = &PROFILES[i];
        if (rf_run_kind(pf->callKind, pf->lang, path, 0))
            scan_defs(pf, &path, 1);
    }
}

static int cmp_file(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* String lookup needs only definitions. Group unique matching files by
   language, batching below the capture helper's 4096-byte command limit. */
void rf_scan_defs(const char **files, int nfiles) {
    if (!nfiles)
        return;
    qsort(files, (size_t)nfiles, sizeof(*files), cmp_file);
    int unique = 0;
    for (int i = 0; i < nfiles; i++)
        if (!unique || strcmp(files[i], files[unique - 1]))
            files[unique++] = files[i];
    int *known = calloc((size_t)unique, sizeof(*known));
    for (int f = 0; f < unique; f++)
        for (size_t i = 0; i < NPROFILES; i++)
            known[f] |= profile_matches(&PROFILES[i], files[f]);
    for (size_t i = 0; i < NPROFILES; i++) {
        const char *batch[128];
        int n = 0;
        size_t bytes = 0;
        for (int f = 0; f < unique; f++) {
            if (known[f] && !profile_matches(&PROFILES[i], files[f]))
                continue;
            /* Shell quoting can expand each byte up to four characters. */
            size_t cost = 4 * strlen(files[f]) + 3;
            if (n && (n == 128 || bytes + cost > 3000)) {
                scan_defs(&PROFILES[i], batch, n);
                n = 0;
                bytes = 0;
            }
            batch[n++] = files[f];
            bytes += cost;
        }
        if (n)
            scan_defs(&PROFILES[i], batch, n);
    }
    free(known);
}
