#include "trim.h"

#include <stdarg.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Text;

static int text_add(Text *t, const char *s, size_t n) {
    size_t need = t->len + n + 1;
    if (need > t->cap) {
        size_t cap = t->cap ? t->cap : 8192;
        while (cap < need)
            cap *= 2;
        char *p = realloc(t->data, cap);
        if (!p)
            return 0;
        t->data = p;
        t->cap = cap;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = '\0';
    return 1;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static int text_addf(Text *t, const char *fmt, ...) {
    va_list ap;
    va_list copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(copy);
        return 0;
    }
    size_t need = t->len + (size_t)n + 1;
    if (need > t->cap) {
        size_t cap = t->cap ? t->cap : 8192;
        while (cap < need)
            cap *= 2;
        char *p = realloc(t->data, cap);
        if (!p) {
            va_end(copy);
            return 0;
        }
        t->data = p;
        t->cap = cap;
    }
    vsnprintf(t->data + t->len, (size_t)n + 1, fmt, copy);
    va_end(copy);
    t->len += (size_t)n;
    return 1;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static void section(Text *t, const char *name) {
    text_addf(t, "\n[%s]\n", name);
}

static void add_capture(Text *t, const char *name, char *out) {
    if (!out || !out[0]) {
        free(out);
        return;
    }
    section(t, name);
    text_add(t, out, strlen(out));
    if (t->len && t->data[t->len - 1] != '\n')
        text_add(t, "\n", 1);
    free(out);
}

static int file_stats(const char *path, size_t *bytes, long *lines) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = 0;
    long nl = 0;
    int ch;
    int last = '\n';
    while ((ch = fgetc(f)) != EOF) {
        n++;
        if (ch == '\n')
            nl++;
        last = ch;
    }
    if (ferror(f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    *bytes = n;
    *lines = nl + (n > 0 && last != '\n');
    return 1;
}

static char *copy_text(const char *s) {
    size_t n = strlen(s);
    char *copy = malloc(n + 1);
    if (copy)
        memcpy(copy, s, n + 1);
    return copy;
}

static void add_exact_range(Text *t, const char *path, long start, long end) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    long line = 1;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (line >= start && line <= end) {
            unsigned char c = (unsigned char)ch;
            text_add(t, (const char *)&c, 1);
        }
        if (ch == '\n') {
            if (line >= end)
                break;
            line++;
        }
    }
    fclose(f);
}

static void add_file_context(Text *t, const char *path, int include_diff) {
    size_t bytes = 0;
    long lines = 0;
    if (!file_stats(path, &bytes, &lines))
        return;

    text_addf(t, "\n[FILE] %s bytes=%zu lines=%ld\n", path, bytes, lines);

    const char *outline[] = {"ast-grep", "outline", path,
                             "--color",  "never",   NULL};
    add_capture(t, "OUTLINE", run_cmd_capture(outline, 5));

    if (include_diff) {
        const char *diff[] = {
            "git", "diff", "--no-ext-diff", "--color=never", "--", path, NULL};
        add_capture(t, "WORKTREE DIFF", run_cmd_capture_raw(diff, 6));
    }

    const char *log[] = {"git", "log", "-5", "--oneline", "--", path, NULL};
    add_capture(t, "RECENT HISTORY", run_cmd_capture(log, 6));

    long head_end = lines < 80 ? lines : 80;
    if (head_end > 0) {
        text_addf(t, "\n[SOURCE %s:1-%ld]\n", path, head_end);
        add_exact_range(t, path, 1, head_end);
    }
    if (lines > 80) {
        long tail_start = lines - 79;
        text_addf(t, "\n[SOURCE %s:%ld-%ld]\n", path, tail_start, lines);
        add_exact_range(t, path, tail_start, lines);
    }
}

static void add_related_outline(Text *t, const char *path) {
    size_t bytes = 0;
    long lines = 0;
    if (!file_stats(path, &bytes, &lines))
        return;
    text_addf(t, "\n[RELATED FILE] %s bytes=%zu lines=%ld\n", path, bytes,
              lines);
    const char *outline[] = {"ast-grep", "outline", path,
                             "--color",  "never",   NULL};
    add_capture(t, "RELATED OUTLINE", run_cmd_capture(outline, 5));
}

static void add_status_file_contexts(Text *t, char *status) {
    char *cursor = status;
    int count = 0;
    while (*cursor && count < 32) {
        char *end = strchr(cursor, '\n');
        if (end)
            *end = '\0';
        if (strlen(cursor) > 3)
            add_file_context(t, cursor + 3, 0);
        count++;
        if (!end)
            break;
        cursor = end + 1;
    }
}

static int add_paths(const char *const *paths, int path_count,
                     const char **args, int n, int capacity) {
    if (path_count == 0)
        return n;
    if (n + path_count + 1 >= capacity)
        return n;
    args[n++] = "--";
    for (int i = 0; i < path_count; i++)
        args[n++] = paths[i];
    return n;
}

static void add_diff_context(Text *t, int path_count,
                             const char *const *paths) {
    const char *status[] = {"git", "status", "--short", NULL};
    char *status_text = run_cmd_capture_raw(status, 3);
    add_capture(t, "GIT STATUS", status_text ? copy_text(status_text) : NULL);

    const char *stat[256] = {0};
    int n = 0;
    stat[n++] = "git";
    stat[n++] = "diff";
    stat[n++] = "--stat";
    stat[n++] = "--color=never";
    n = add_paths(paths, path_count, stat, n, 256);
    add_capture(t, "DIFF STAT", run_cmd_capture_raw(stat, n));

    const char *names[] = {"git", "status", "--short", NULL};
    char *name_text = run_cmd_capture(names, 3);
    add_capture(t, "CHANGED FILES", name_text ? copy_text(name_text) : NULL);

    const char *diff[256] = {0};
    n = 0;
    diff[n++] = "git";
    diff[n++] = "diff";
    diff[n++] = "--no-ext-diff";
    diff[n++] = "--color=never";
    n = add_paths(paths, path_count, diff, n, 256);
    add_capture(t, "EXACT DIFF", run_cmd_capture_raw(diff, n));

    free(name_text);
    if (status_text) {
        add_status_file_contexts(t, status_text);
        free(status_text);
    }
}

static void add_query_context(Text *t, const char *pattern, const char *path) {
    text_addf(t, "\n[QUERY] %s path=%s\n", pattern, path);
    const char *args[] = {"rg",           "-n", "-C",    "3",  "--color=never",
                          "--no-heading", "--", pattern, path, NULL};
    add_capture(t, "MATCHES AND REFERENCES", run_cmd_capture_raw(args, 9));
    size_t bytes = 0;
    long lines = 0;
    if (file_stats(path, &bytes, &lines)) {
        add_file_context(t, path, 1);
    } else {
        const char *files[] = {"rg", "-l", "--color=never", "--", pattern,
                               path, NULL};
        char *related = run_cmd_capture_raw(files, 6);
        if (related) {
            char *cursor = related;
            int count = 0;
            while (*cursor && count < 32) {
                char *end = strchr(cursor, '\n');
                if (end)
                    *end = '\0';
                if (*cursor) {
                    add_related_outline(t, cursor);
                    count++;
                }
                if (!end)
                    break;
                cursor = end + 1;
            }
            free(related);
        }
    }
}

static void emit_context(Text *t) {
    if (!t->data)
        return;
    size_t compact_n = 0;
    char *compact = compact_paths(t->data, &compact_n);
    if (!compact)
        compact = t->data;
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
        printf("[CONTEXT_TRUNCATED:%zu/%zu] use trim context with a narrower "
               "scope\n",
               cut, compact_n);
    }
    if (compact != t->data)
        free(compact);
}

void cmd_context(int argc, const char *const *argv) {
    Text text = {0};
    if (argc == 0 || !strcmp(argv[0], "diff")) {
        int path_count = argc > 0 ? argc - 1 : 0;
        add_diff_context(&text, path_count, argc > 0 ? argv + 1 : NULL);
    } else if (!strcmp(argv[0], "--query")) {
        if (argc < 2) {
            fprintf(stderr,
                    "error: usage: trim context --query <pattern> [path]\n");
            return;
        }
        add_query_context(&text, argv[1], argc > 2 ? argv[2] : ".");
    } else if (argc == 1) {
        size_t bytes = 0;
        long lines = 0;
        if (file_stats(argv[0], &bytes, &lines)) {
            (void)bytes;
            (void)lines;
            add_file_context(&text, argv[0], 1);
        } else {
            add_query_context(&text, argv[0], ".");
        }
    } else {
        add_query_context(&text, argv[0], argv[1]);
    }
    emit_context(&text);
    free(text.data);
}
