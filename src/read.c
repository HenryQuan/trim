#include "trim.h"
#include <stdarg.h>

/* growable output buffer that read/lines build into, then compact + emit once */
static char *obuf = NULL;
static size_t olen = 0, ocap = 0;
static int defer_flush = 0;

static void oadd(const char *s) {
    size_t n = strlen(s);
    if (olen + n + 1 > ocap) {
        ocap = ocap ? ocap * 2 : 8192;
        while (ocap < olen + n + 1) ocap *= 2;
        obuf = realloc(obuf, ocap);
    }
    memcpy(obuf + olen, s, n);
    olen += n;
}

static void ofmt(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    oadd(tmp);
}

static void oflush(void) {
    if (!obuf) return;
    obuf[olen] = '\0';
    size_t cn = 0;
    char *comp = compact_paths(obuf, &cn);
    fwrite(comp, 1, cn, stdout);
    if (cn && comp[cn - 1] != '\n') fputc('\n', stdout);
    free(comp);
    free(obuf);
    obuf = NULL; olen = 0; ocap = 0;
}

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
        if (!emitted) ofmt("[T:%d/%zu]FILE:%s@%d", (int)MAX_LINES, total, path, lineno);
        oadd(pre);
        oadd(line);
        emitted++;
    }
    fclose(f);
    if (!emitted) ofmt("[T:%d/%zu]FILE:%s@0", (int)MAX_CHARS, total, path);
    if (truncated) oadd(" — prefer trim par \"a\" \"b\" ...");
    oadd("\n");
    if (!defer_flush) oflush();
}

/* smart read: small file -> whole content; large file -> outline + first/last 10 + hint */
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

    defer_flush = 1;
    char *oa[] = {"ast-grep", "outline", (char*)path, "--color", "never", NULL};
    char *ol = run_cmd_capture(oa, 5);
    if (ol) { oadd(ol); if (ol[0] && ol[strlen(ol) - 1] != '\n') oadd("\n"); free(ol); }
    read_lines(path, 1, 10);
    oadd("\n  ... (skipped) ...\n");
    read_lines(path, lines - 9, lines);
    ofmt("\n[LARGE %ld lines] use trim lines %s <start> <end> to read a range\n", lines, path);
    defer_flush = 0;
    oflush();
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
