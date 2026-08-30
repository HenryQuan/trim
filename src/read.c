#include "trim.h"

void read_lines(const char *path, int start, int end) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", path); exit(1); }
    char tmp[4096];
    size_t total = 0;
    int ln = 0;
    while (fgets(tmp, sizeof(tmp), f)) {
        ln++;
        if (ln < start) continue;
        if (end > 0 && ln > end) break;
        total += strlen(tmp);
    }
    rewind(f);

    char line[4096];
    int lineno = 0, blanks = 0, emitted = 0, truncated = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        if (lineno < start) continue;
        if (end > 0 && lineno > end) break;
        size_t len = strlen(line);
        len = strip_ansi(line, len);
        len = normalize_newlines(line, len);
        len = collapse_escapes(line, len);
        line[len] = '\0';
        len = lstrip(line);
        len = collapse_spaces(line, len);
        if (is_blank_line(line)) {
            if (blanks >= 1) continue;
            blanks++;
        } else {
            blanks = 0;
        }
        if (len && line[len - 1] == '\n') line[--len] = '\0';
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r'))
            line[--len] = '\0';

        char pre[24];
        snprintf(pre, sizeof(pre), "|%d:", lineno);
        if (emitted >= MAX_LINES) { truncated = 1; break; }
        if (!emitted) printf("[T:%d/%zu]FILE:%s@%d", (int)MAX_LINES, total, path, lineno);
        fputs(pre, stdout);
        fputs(line, stdout);
        emitted++;
    }
    fclose(f);
    if (!emitted) printf("[T:%d/%zu]FILE:%s@0", (int)MAX_CHARS, total, path);
    if (truncated) printf(" — %s", HINT_PAR);
    printf("\n");
}

/* smart read: small file -> whole content (1 step); large file -> outline + preview + hint (1 step) */
void read_smart(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", path); exit(1); }
    char tmp[4096];
    long lines = 0;
    while (fgets(tmp, sizeof(tmp), f)) lines++;
    fclose(f);

    if (lines <= (long)MAX_LINES) {
        read_lines(path, 1, 0);
        return;
    }

    char *oa[] = {"ast-grep", "outline", (char*)path, "--color", "never", NULL};
    run_cmd(oa, 5);
    read_lines(path, 1, 10);
    printf("\n  ... (skipped) ...\n");
    read_lines(path, lines - 9, lines);
    printf("\n[LARGE %ld lines] use trim lines %s <start> <end> to read a range\n", lines, path);
}

void cmd_outline(int argc, char **argv) {
    char *args[256]; int n = 0;
    args[n++] = "ast-grep"; args[n++] = "outline";
    for (int i = 2; i < argc && n < 255; i++) args[n++] = argv[i];
    args[n++] = "--color"; args[n++] = "never";
    args[n] = NULL;
    run_cmd(args, n);
}

void cmd_diff(const char *file) {
    if (file) {
        char *args[] = {"git", "diff", "--color=never", (char*)file, NULL};
        run_cmd(args, 4);
    } else {
        char *args[] = {"git", "diff", "--color=never", NULL};
        run_cmd(args, 3);
    }
}

void cmd_blame(const char *file) {
    char *args[] = {"git", "blame", (char*)file, NULL};
    run_cmd(args, 3);
}

void cmd_log(int argc, char **argv) {
    char *args[256]; int n = 0;
    args[n++] = "git"; args[n++] = "log";
    for (int i = 2; i < argc && n < 255; i++) args[n++] = argv[i];
    args[n] = NULL;
    run_cmd(args, n);
}
