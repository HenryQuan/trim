#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
  #define popen _popen
  #define pclose _pclose
#else
  #include <unistd.h>
  #include <sys/wait.h>
#endif

int _CRT_glob = 0;

static size_t MAX_CHARS = 2222;
static size_t MAX_LINES = 512;

static void init_max_chars(void) {
    char *env = getenv("TRIM_MAX_CHARS");
    if (env) {
        unsigned long v = strtoul(env, NULL, 10);
        if (v > 0) MAX_CHARS = v < 65536 ? (size_t)v : 65536;
    }
    char *envl = getenv("TRIM_MAX_LINES");
    if (envl) {
        unsigned long v = strtoul(envl, NULL, 10);
        if (v > 0) MAX_LINES = v < 100000 ? (size_t)v : 100000;
    }
}

static const char *HELP =
    "trim: capped-output wrapper around any command\n"
    "\n"
    "Always use trim (alias trm) instead of raw commands -- output is capped to save tokens.\n"
    "Follow understand -> ask -> inspect -> simplify -> change -> verify -> report\n"
    "\n"
    "Usage:\n"
    "  trim <command> [args]          run any command, output capped\n"
    "  trim rg <args>                 run ripgrep\n"
    "  trim sg <args>                 run ast-grep\n"
    "  trim fd <args>                 run fd\n"
    "  trim lines <file> <start> [<end>]  print lines start..end (l/sed aliases; $ = EOF)\n"
    "  trim read|cat|print <file>     read whole file with output cap (prefer trim lines)\n"
    "  trim outline <file>            extract function/class signatures (ast-grep)\n"
    "  trim diff [<file>]             git diff (read-only)\n"
    "  trim blame <file>              git blame (read-only)\n"
    "  trim log [<args>]              git log (read-only)\n"
    "  trim par \"cmd1\" \"cmd2\" ...    batch commands, each output capped\n"
    "  trim lines honors its range up to MAX_LINES (default 512, env TRIM_MAX_LINES);\n"
    "  prefer trim lines over trim read/cat/print to avoid whole-file reads.\n";

static const char *HINT =
    "\n[TRUNCATED:%zu/%zuc] %s\n";
#define HINT_PAR "prefer trim par \"a\" \"b\" ..."
#define HINT_RG  "capped — try trim outline <file>"
#define HINT_OUT "capped — try trim rg <pat> <file>"

static const char *pick_hint(const char *cmd) {
    if (!strncmp(cmd, "rg ", 3)) return HINT_RG;
    if (!strncmp(cmd, "ast-grep", 8)) return HINT_OUT;
    return HINT_PAR;
}

static int is_read(const char *s) {
    return !strcmp(s, "read") || !strcmp(s, "cat") || !strcmp(s, "print");
}

static int is_lines(const char *s) {
    return !strcmp(s, "lines") || !strcmp(s, "l") || !strcmp(s, "sed");
}

static void cap(const char *s, int truncated, size_t total_chars, const char *hint) {
    size_t len = strlen(s);
    if (truncated || len > MAX_CHARS) {
        size_t n = len < MAX_CHARS ? len : MAX_CHARS;
        fwrite(s, 1, n, stdout);
        printf(HINT, MAX_CHARS, total_chars, hint ? hint : HINT_PAR);
    } else {
        fputs(s, stdout);
    }
}

/* strip ANSI escape codes (ESC[<params><letter>) in place; returns new length */
static size_t strip_ansi(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0x1b && i + 1 < n && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')) j++;
            if (j < n && ((s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= 'a' && s[j] <= 'z'))) {
                i = j;
                continue;
            }
        }
        s[w++] = s[i];
    }
    return w;
}

/* collapse runs of 3+ newlines into 2 in place; returns new length */
static size_t collapse_blanks(char *s, size_t n) {
    size_t w = 0, nl = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') {
            nl++;
            if (nl > 2) continue;
        } else {
            nl = 0;
        }
        s[w++] = s[i];
    }
    return w;
}

/* normalize \r\n -> \n and lone \r -> \n in place; returns new length */
static size_t normalize_newlines(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') continue;
            s[w++] = '\n';
        } else {
            s[w++] = s[i];
        }
    }
    return w;
}

/* collapse literal backslash escapes (\r\n, \n, \r, \t) to a single space; returns new length */
static size_t collapse_escapes(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[i + 1];
            if (c == 'n' || c == 't' || c == 'r') {
                if (c == 'r' && i + 2 < n && s[i + 2] == 'n') i++; /* \r\n -> skip the n too */
                s[w++] = ' ';
                i++;
                continue;
            }
        }
        s[w++] = s[i];
    }
    return w;
}

/* strip leading spaces/tabs in place (NUL-terminated); returns new length */
static size_t lstrip(char *s) {
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s) - i + 1);
    return strlen(s);
}

/* strip leading spaces/tabs from every line in a buffer; returns new length */
static size_t lstrip_lines(char *s, size_t n) {
    size_t w = 0, i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != '\n') j++;
        size_t k = i;
        while (k < j && (s[k] == ' ' || s[k] == '\t')) k++;
        size_t len = j - k;
        memmove(s + w, s + k, len);
        w += len;
        if (j < n) s[w++] = '\n';
        i = j + 1;
    }
    return w;
}

/* collapse runs of 2+ spaces/tabs to one space in place; returns new length */
static size_t collapse_spaces(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if ((s[i] == ' ' || s[i] == '\t') && w > 0 && (s[w - 1] == ' ' || s[w - 1] == '\t'))
            continue;
        s[w++] = s[i];
    }
    return w;
}

static int is_blank_line(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return *s == '\0';
}

#ifdef _WIN32
static void append_escaped(char *buf, int *pos, int bufsize, const char *arg) {
    if (*pos >= bufsize - 2) return;
    buf[(*pos)++] = '"';
    for (const char *p = arg; *p; p++) {
        if (*pos >= bufsize - 3) break;
        if (*p == '%') buf[(*pos)++] = '%';
        buf[(*pos)++] = *p;
    }
    if (*pos < bufsize) {
        buf[(*pos)++] = '"';
        buf[*pos] = '\0';
    }
}
#else
static void append_escaped(char *buf, int *pos, int bufsize, const char *arg) {
    if (*pos >= bufsize - 2) return;
    buf[(*pos)++] = '\'';
    for (const char *p = arg; *p; p++) {
        if (*p == '\'') {
            if (*pos >= bufsize - 4) break;
            buf[(*pos)++] = '\''; buf[(*pos)++] = '\\';
            buf[(*pos)++] = '\''; buf[(*pos)++] = '\'';
        } else {
            if (*pos >= bufsize - 1) break;
            buf[(*pos)++] = *p;
        }
    }
    if (*pos < bufsize) {
        buf[(*pos)++] = '\'';
        buf[*pos] = '\0';
    }
}
#endif

static int run_cmd_str(const char *cmd) {
    char buf[4220];
    size_t blen = strlen(cmd);
    if (blen + 6 >= sizeof(buf)) blen = sizeof(buf) - 7;
    memcpy(buf, cmd, blen);
    memcpy(buf + blen, " 2>&1", 6);

    FILE *p = popen(buf, "r");
    if (!p) { fprintf(stderr, "error: failed to run command\n"); return 1; }

    char out[65536];
    size_t n = 0;
    int ch;
    while ((ch = fgetc(p)) != EOF && n < sizeof(out) - 1)
        out[n++] = (char)ch;
    out[n] = '\0';
    size_t total = n;
    while ((ch = fgetc(p)) != EOF) total++;
    n = strip_ansi(out, n);
    n = normalize_newlines(out, n);
    n = collapse_blanks(out, n);
    n = lstrip_lines(out, n);
    out[n] = '\0';
    cap(out, n > MAX_CHARS, total, pick_hint(cmd));
    int rc = pclose(p);
#ifndef _WIN32
    if (rc == -1) return 1;
    if (rc && WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
    return rc;
}

static void run_cmd(char **args, int argc) {
    char buf[4096];
    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i) buf[pos++] = ' ';
        if (i == 0) {
            int len = strlen(args[i]);
            if (pos + len < (int)sizeof(buf)) {
                memcpy(buf + pos, args[i], len);
                pos += len;
                buf[pos] = '\0';
            }
        } else {
            append_escaped(buf, &pos, sizeof(buf), args[i]);
        }
    }

    int rc = run_cmd_str(buf);
    if (rc > 1) exit(rc);
}

/* trim par "cmd1" "cmd2" ... — run each command, each output capped separately */
static void cmd_par(int argc, char **argv) {
    int shown = 0;
    for (int i = 0; i < argc; i++) {
        if (!argv[i] || !argv[i][0]) continue;
        if (shown++) printf("\n");
        printf("[%d] %s\n", i + 1, argv[i]);
        run_cmd_str(argv[i]);
    }
}

static void read_lines(const char *path, int start, int end);

/* smart read: small file -> whole content (1 step); large file -> outline + preview + hint (1 step) */
static void read_smart(const char *path) {
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
    read_lines(path, 1, 40);
    printf("\n[LARGE %ld lines] use trim lines %s <start> <end> to read a range; batch reads with trim par\n",
           lines, path);
}

static void read_lines(const char *path, int start, int end) {
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

static void cmd_outline(int argc, char **argv) {
    char *args[256]; int n = 0;
    args[n++] = "ast-grep"; args[n++] = "outline";
    for (int i = 2; i < argc && n < 255; i++) args[n++] = argv[i];
    args[n++] = "--color"; args[n++] = "never";
    args[n] = NULL;
    run_cmd(args, n);
}

static void cmd_diff(const char *file) {
    if (file) {
        char *args[] = {"git", "diff", "--color=never", (char*)file, NULL};
        run_cmd(args, 4);
    } else {
        char *args[] = {"git", "diff", "--color=never", NULL};
        run_cmd(args, 3);
    }
}

static void cmd_blame(const char *file) {
    char *args[] = {"git", "blame", (char*)file, NULL};
    run_cmd(args, 3);
}

static void cmd_log(int argc, char **argv) {
    char *args[256]; int n = 0;
    args[n++] = "git"; args[n++] = "log";
    for (int i = 2; i < argc && n < 255; i++) args[n++] = argv[i];
    args[n] = NULL;
    run_cmd(args, n);
}

int main(int argc, char **argv) {
    init_max_chars();
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(HELP, stdout);
        return 0;
    }

    char *cmd = argv[1];

    if (is_read(cmd)) {
        if (argc < 3) { fprintf(stderr, "error: missing file path\n"); return 1; }
        read_smart(argv[2]);
        return 0;
    }

    if (is_lines(cmd)) {
        if (argc < 4) { fprintf(stderr, "error: usage: trim lines <file> <start> [<end>]\n"); return 1; }
        int start = atoi(argv[3]);
        int end = 0;
        if (argc > 4 && strcmp(argv[4], "$")) end = atoi(argv[4]);
        if (start < 1) { fprintf(stderr, "error: start line must be >= 1\n"); return 1; }
        if (end > 0 && end < start) { fprintf(stderr, "error: end line must be >= start\n"); return 1; }
        read_lines(argv[2], start, end);
        return 0;
    }

    if (!strcmp(cmd, "outline")) {
        if (argc < 3) { fprintf(stderr, "error: usage: trim outline <file>\n"); return 1; }
        cmd_outline(argc, argv);
        return 0;
    }

    if (!strcmp(cmd, "diff")) {
        cmd_diff(argc > 2 ? argv[2] : NULL);
        return 0;
    }

    if (!strcmp(cmd, "blame")) {
        if (argc < 3) { fprintf(stderr, "error: usage: trim blame <file>\n"); return 1; }
        cmd_blame(argv[2]);
        return 0;
    }

    if (!strcmp(cmd, "log")) {
        cmd_log(argc, argv);
        return 0;
    }

    if (!strcmp(cmd, "par")) {
        if (argc < 3) { fprintf(stderr, "error: usage: trim par \"cmd1\" \"cmd2\" ...\n"); return 1; }
        cmd_par(argc - 2, argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "rg")) {
        argv[1] = "rg";
    } else if (!strcmp(cmd, "sg") || !strcmp(cmd, "ast-grep")) {
        argv[1] = "ast-grep";
    } else if (!strcmp(cmd, "fd")) {
        argv[1] = "fd";
    }
    /* anything else: run as-is, output capped */

    run_cmd(argv + 1, argc - 1);
    return 0;
}
