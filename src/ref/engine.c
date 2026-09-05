/* engine.c — ast-grep runs, small helpers, compacted output */
#include "../trim.h"
#include "rf.h"

/* kind search; collect call sites (want_defs=0) or enclosing defs (1);
   returns hit count (0 = kind produced nothing) */
int rf_run_kind(const char *kind, const char *lang, const char *path,
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
    GrepHit *hits = rf_parse_hits(json, &n);
    for (int i = 0; i < n; i++) {
        GrepHit *h = &hits[i];
        for (char *t = h->file; *t; t++)
            if (*t == '\\')
                *t = '/'; /* normalize sg's mixed separators */
        char *name = want_defs ? rf_name_from_text(h->text)
                               : rf_callee_from_call(h->text);
        if (name) {
            if (want_defs)
                rf_add_def(h->file, name, h->start,
                           h->end > 0 ? h->end : h->start);
            else
                rf_add_site(name, h->file, h->start, h->text);
        }
        free(name);
        free(h->text);
        free(h->file);
    }
    free(hits);
    free(json);
    return n;
}

int rf_visited_has(char **visited, int n, const char *s) {
    for (int i = 0; i < n; i++)
        if (!strcmp(visited[i], s))
            return 1;
    return 0;
}

char **rf_push_name(char **arr, int *n, int *cap, const char *s) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        arr = realloc(arr, (size_t)*cap * sizeof(char *));
    }
    arr[(*n)++] = rf_sdup(s);
    return arr;
}

/* first line of text, trimmed of leading space; tabs/CR folded to space */
void rf_first_line(const char *text, char *out, size_t cap) {
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

/* text-level rg fallback when ast-grep is missing or nothing indexed */
void rf_rg_fallback(Buf *out, const char *target, const char *path) {
    rf_buf_add(out, "ast-grep indexes empty — rg fallback (text-level):\n");
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
        rf_buf_add(out, raw);
    else
        rf_buf_add(out, "no matches\n");
    free(raw);
}

/* emit through the shared lossless compaction, like context/read;
   tag names the feature in the truncation notice */
void rf_emit_capped(Buf *b, const char *tag) {
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
        printf("[%s_TRUNCATED:%zu/%zuc] narrow path or raise TRIM_MAX_CHARS\n",
               tag, cut, compact_n);
    }
    if (compact != b->data)
        free(compact);
    free(b->data);
    b->data = NULL;
}

void rf_emit_ref(Buf *b) { rf_emit_capped(b, "REF"); }
