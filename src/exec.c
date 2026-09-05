#include "trim.h"

#ifdef _WIN32
static void append_escaped(char *buf, int *pos, int bufsize, const char *arg) {
    if (*pos >= bufsize - 2)
        return;
    buf[(*pos)++] = '"';
    for (const char *p = arg; *p; p++) {
        if (*pos >= bufsize - 3)
            break;
        if (*p == '%')
            buf[(*pos)++] = '%';
        buf[(*pos)++] = *p;
    }
    if (*pos < bufsize) {
        buf[(*pos)++] = '"';
        buf[*pos] = '\0';
    }
}
#else
static void append_escaped(char *buf, int *pos, int bufsize, const char *arg) {
    if (*pos >= bufsize - 2)
        return;
    buf[(*pos)++] = '\'';
    for (const char *p = arg; *p; p++) {
        if (*p == '\'') {
            if (*pos >= bufsize - 4)
                break;
            buf[(*pos)++] = '\'';
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = '\'';
            buf[(*pos)++] = '\'';
        } else {
            if (*pos >= bufsize - 1)
                break;
            buf[(*pos)++] = *p;
        }
    }
    if (*pos < bufsize) {
        buf[(*pos)++] = '\'';
        buf[*pos] = '\0';
    }
}
#endif

int run_cmd_str(const char *cmd) {
    char buf[4220];
    size_t blen = strlen(cmd);
    if (blen + 6 >= sizeof(buf))
        blen = sizeof(buf) - 7;
    memcpy(buf, cmd, blen);
    memcpy(buf + blen, " 2>&1", 6);

    FILE *p = popen(buf, "r");
    if (!p) {
        fprintf(stderr, "error: failed to run command\n");
        return 1;
    }

    char out[65536];
    size_t n = 0;
    int ch;
    while ((ch = fgetc(p)) != EOF && n < sizeof(out) - 1)
        out[n++] = (char)ch;
    out[n] = '\0';
    while ((ch = fgetc(p)) != EOF) { /* drain rest so pclose doesn't block */
    }
    n = strip_ansi(out, n);
    n = normalize_newlines(out, n);
    out[n] = '\0';
    /* compact first (lossless), then cap at a line boundary */
    size_t cn = 0;
    char *comp = compact_paths(out, &cn);
    if (cn > MAX_CHARS) {
        size_t cut = MAX_CHARS;
        while (cut > 0 && comp[cut - 1] != '\n')
            cut--;
        if (cut == 0)
            cut = MAX_CHARS;
        fwrite(comp, 1, cut, stdout);
        printf("\n[TRUNCATED:%zu/%zu] %s\n", cut, cn, pick_hint_ctx(cmd));
    } else {
        fwrite(comp, 1, cn, stdout);
        if (cn && comp[cn - 1] != '\n')
            fputc('\n', stdout);
    }
    free(comp);
    int rc = pclose(p);
#ifndef _WIN32
    if (rc == -1)
        return 1;
    if (rc && WIFEXITED(rc))
        rc = WEXITSTATUS(rc);
#endif
    return rc;
}

void run_cmd(const char *const *args, int argc) {
    char buf[4096];
    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i)
            buf[pos++] = ' ';
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
    if (rc > 1)
        exit(rc);
}

/* capture a command's full output (no cap), post-processed; caller frees */
static char *capture_command(const char *cmd, int normalize) {
    char buf[4220];
    size_t blen = strlen(cmd);
    if (blen + 6 >= sizeof(buf))
        blen = sizeof(buf) - 7;
    memcpy(buf, cmd, blen);
    memcpy(buf + blen, " 2>&1", 6);

    FILE *p = popen(buf, "r");
    if (!p) {
        fprintf(stderr, "error: failed to run command\n");
        return NULL;
    }

    size_t cap = 65536, n = 0;
    char *out = malloc(cap);
    if (!out) {
        pclose(p);
        return NULL;
    }
    int ch;
    while ((ch = fgetc(p)) != EOF) {
        if (n + 2 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        out[n++] = (char)ch;
    }
    out[n] = '\0';
    pclose(p);
    if (normalize) {
        n = strip_ansi(out, n);
        n = normalize_newlines(out, n);
    }
    out[n] = '\0';
    return out;
}

char *run_capture(const char *cmd) { return capture_command(cmd, 1); }

char *run_capture_raw(const char *cmd) { return capture_command(cmd, 0); }

char *run_cmd_capture(const char *const *args, int argc) {
    char buf[4096];
    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i)
            buf[pos++] = ' ';
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
    return run_capture(buf);
}

char *run_cmd_capture_raw(const char *const *args, int argc) {
    char buf[4096];
    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i)
            buf[pos++] = ' ';
        if (i == 0) {
            int len = strlen(args[i]);
            if (pos + len < (int)sizeof(buf)) {
                memcpy(buf + pos, args[i], (size_t)len);
                pos += len;
                buf[pos] = '\0';
            }
        } else {
            append_escaped(buf, &pos, sizeof(buf), args[i]);
        }
    }
    return run_capture_raw(buf);
}

/* trim par "cmd1" "cmd2" ... -- run each command, each output capped separately
 */
void cmd_par(int argc, const char *const *argv) {
    int shown = 0;
    for (int i = 0; i < argc; i++) {
        if (!argv[i] || !argv[i][0])
            continue;
        if (shown++)
            printf("\n");
        printf("[%d] %s\n", i + 1, argv[i]);
        run_cmd_str(argv[i]);
    }
}
