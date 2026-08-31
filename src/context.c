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

#define MAX_CONTEXT_FILES 32

typedef struct {
    const char *path[MAX_CONTEXT_FILES];
    char state[MAX_CONTEXT_FILES][4];
    int count;
} ContextFiles;

static int file_index(const ContextFiles *files, const char *path) {
    for (int i = 0; i < files->count; i++)
        if (!strcmp(files->path[i], path))
            return i;
    return -1;
}

static void add_file(ContextFiles *files, const char *path, const char *state) {
    if (!path || !path[0] || file_index(files, path) >= 0 ||
        files->count >= MAX_CONTEXT_FILES)
        return;
    files->path[files->count] = path;
    snprintf(files->state[files->count], sizeof(files->state[0]), "%s", state);
    files->count++;
}

static void collect_status(ContextFiles *files, char *status) {
    char *cursor = status;
    while (*cursor && files->count < MAX_CONTEXT_FILES) {
        char *end = strchr(cursor, '\n');
        if (end)
            *end = '\0';
        if (strlen(cursor) > 3) {
            char state[4] = {cursor[0], cursor[1], '\0', '\0'};
            add_file(files, cursor + 3, state);
        }
        if (!end)
            break;
        cursor = end + 1;
    }
}

static void add_manifest(Text *t, const ContextFiles *files) {
    section(t, "FILES");
    for (int i = 0; i < files->count; i++) {
        size_t bytes = 0;
        long lines = 0;
        if (file_stats(files->path[i], &bytes, &lines))
            text_addf(t, "F%d %s %s bytes=%zu lines=%ld\n", i + 1,
                      files->state[i], files->path[i], bytes, lines);
        else
            text_addf(t, "F%d %s %s [unavailable]\n", i + 1, files->state[i],
                      files->path[i]);
    }
}

static void add_index(Text *t, const ContextFiles *files) {
    section(t, "INDEX");
    for (int i = 0; i < files->count; i++)
        text_addf(t, "F%d: outline, history, source; diff=%s\n", i + 1,
                  files->state[i][0] == '?' ? "untracked" : "available");
}

static void add_capture_id(Text *t, const char *name, int id, char *out) {
    if (!out || !out[0]) {
        free(out);
        return;
    }
    text_addf(t, "\n[%s F%d]\n", name, id);
    text_add(t, out, strlen(out));
    if (t->len && t->data[t->len - 1] != '\n')
        text_add(t, "\n", 1);
    free(out);
}

static int path_directory(const char *path, char *dir, size_t cap) {
    size_t n = strlen(path);
    if (n == 0 || n >= cap)
        return 0;
    memcpy(dir, path, n + 1);
    size_t slash = n;
    while (slash > 0 && dir[slash - 1] != '/' && dir[slash - 1] != '\\')
        slash--;
    if (slash == 0)
        snprintf(dir, cap, ".");
    else if (slash == 1)
        dir[1] = '\0';
    else
        dir[slash - 1] = '\0';
    return 1;
}

static int find_repo_root(const char *path, char *root, size_t cap) {
    char dir[PATH_MAX];
    size_t bytes = 0;
    long lines = 0;
    if (!file_stats(path, &bytes, &lines) &&
        !path_directory(path, dir, sizeof(dir)))
        return 0;
    if (file_stats(path, &bytes, &lines))
        path_directory(path, dir, sizeof(dir));
    else
        snprintf(dir, sizeof(dir), "%s", path);

    const char *args[] = {"git", "-C", dir, "rev-parse", "--show-toplevel",
                          NULL};
    char *out = run_cmd_capture_raw(args, 5);
    if (!out)
        return 0;
    char *end = strchr(out, '\n');
    if (end)
        *end = '\0';
    int ok = out[0] && strlen(out) < cap;
    if (ok)
        memcpy(root, out, strlen(out) + 1);
    free(out);
    return ok;
}

static void relative_path(const char *root, const char *path, char *out,
                          size_t cap) {
    size_t root_len = strlen(root);
    const char *relative = path;
    if (root_len > 0 && !strncmp(path, root, root_len) &&
        (path[root_len] == '/' || path[root_len] == '\\'))
        relative = path + root_len + 1;
    snprintf(out, cap, "%s", relative);
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

static void add_file_context(Text *t, const char *path, int id,
                             int include_diff) {
    size_t bytes = 0;
    long lines = 0;
    if (!file_stats(path, &bytes, &lines))
        return;

    char root[PATH_MAX];
    char relative[PATH_MAX];
    int has_repo = find_repo_root(path, root, sizeof(root));
    if (has_repo)
        relative_path(root, path, relative, sizeof(relative));

    text_addf(t, "\n[FILE F%d] %s bytes=%zu lines=%ld\n", id, path, bytes,
              lines);

    const char *outline[] = {"ast-grep", "outline", path,
                             "--color",  "never",   NULL};
    add_capture_id(t, "OUTLINE", id, run_cmd_capture(outline, 5));

    if (include_diff && has_repo) {
        const char *diff[] = {
            "git",           "-C", root,     "diff", "--no-ext-diff",
            "--color=never", "--", relative, NULL};
        add_capture_id(t, "WORKTREE DIFF", id, run_cmd_capture_raw(diff, 8));
    }

    if (has_repo) {
        const char *log[] = {"git",       "-C", root,     "log", "-5",
                             "--oneline", "--", relative, NULL};
        add_capture_id(t, "RECENT HISTORY", id, run_cmd_capture(log, 8));
    }

    long head_end = lines < 80 ? lines : 80;
    if (head_end > 0) {
        text_addf(t, "\n[SOURCE F%d %s:1-%ld]\n", id, path, head_end);
        add_exact_range(t, path, 1, head_end);
    }
    if (lines > 80) {
        long tail_start = lines - 79;
        text_addf(t, "\n[SOURCE F%d %s:%ld-%ld]\n", id, path, tail_start,
                  lines);
        add_exact_range(t, path, tail_start, lines);
    }
}

static void add_related_outline(Text *t, const char *path, int id) {
    size_t bytes = 0;
    long lines = 0;
    if (!file_stats(path, &bytes, &lines))
        return;
    text_addf(t, "\n[RELATED FILE F%d] %s bytes=%zu lines=%ld\n", id, path,
              bytes, lines);
    const char *outline[] = {"ast-grep", "outline", path,
                             "--color",  "never",   NULL};
    add_capture_id(t, "RELATED OUTLINE", id, run_cmd_capture(outline, 5));
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
    ContextFiles files = {0};
    char *status_copy = status_text ? copy_text(status_text) : NULL;
    if (status_text)
        collect_status(&files, status_text);
    for (int i = 0; i < path_count; i++)
        add_file(&files, paths[i], "  ");

    text_addf(t, "[CONTEXT v1] mode=diff files=%d\n", files.count);
    add_capture(t, "GIT STATUS", status_copy);
    add_manifest(t, &files);
    add_index(t, &files);

    const char *stat[256] = {0};
    int n = 0;
    stat[n++] = "git";
    stat[n++] = "diff";
    stat[n++] = "--stat";
    stat[n++] = "--color=never";
    n = add_paths(paths, path_count, stat, n, 256);
    add_capture(t, "DIFF STAT", run_cmd_capture_raw(stat, n));

    const char *diff[256] = {0};
    n = 0;
    diff[n++] = "git";
    diff[n++] = "diff";
    diff[n++] = "--no-ext-diff";
    diff[n++] = "--color=never";
    n = add_paths(paths, path_count, diff, n, 256);
    add_capture(t, "EXACT DIFF", run_cmd_capture_raw(diff, n));

    for (int i = 0; i < files.count; i++)
        add_file_context(t, files.path[i], i + 1, 0);
    if (status_text)
        free(status_text);
}

static void add_query_context(Text *t, const char *pattern, const char *path) {
    ContextFiles files = {0};
    size_t bytes = 0;
    long lines = 0;
    int is_file = file_stats(path, &bytes, &lines);
    char *related = NULL;
    if (is_file) {
        add_file(&files, path, "  ");
    } else {
        const char *files_cmd[] = {"rg", "-l", "--color=never", "--", pattern,
                                   path, NULL};
        related = run_cmd_capture_raw(files_cmd, 6);
        if (related) {
            char *cursor = related;
            while (*cursor && files.count < MAX_CONTEXT_FILES) {
                char *end = strchr(cursor, '\n');
                if (end)
                    *end = '\0';
                add_file(&files, cursor, "  ");
                if (!end)
                    break;
                cursor = end + 1;
            }
        }
    }

    text_addf(t, "[CONTEXT v1] mode=query files=%d pattern=%s path=%s\n",
              files.count, pattern, path);
    add_manifest(t, &files);
    add_index(t, &files);
    const char *args[] = {"rg",           "-n", "-C",    "8",  "--color=never",
                          "--no-heading", "--", pattern, path, NULL};
    add_capture(t, "MATCHES AND REFERENCES", run_cmd_capture_raw(args, 9));
    if (is_file) {
        add_file_context(t, path, 1, 1);
    } else {
        for (int i = 0; i < files.count; i++) {
            size_t related_bytes = 0;
            long related_lines = 0;
            if (file_stats(files.path[i], &related_bytes, &related_lines) &&
                related_lines <= 120)
                add_file_context(t, files.path[i], i + 1, 0);
            else
                add_related_outline(t, files.path[i], i + 1);
        }
    }
    free(related);
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
            ContextFiles files = {0};
            add_file(&files, argv[0], "  ");
            text_addf(&text, "[CONTEXT v1] mode=file files=%d\n", files.count);
            add_manifest(&text, &files);
            add_index(&text, &files);
            add_file_context(&text, argv[0], 1, 1);
        } else {
            add_query_context(&text, argv[0], ".");
        }
    } else {
        add_query_context(&text, argv[0], argv[1]);
    }
    emit_context(&text);
    free(text.data);
}
