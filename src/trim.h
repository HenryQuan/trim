#ifndef TRIM_H
#define TRIM_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#ifdef __MINGW_PRINTF_FORMAT
#define TRIM_PRINTF(fmt, args)                                                 \
    __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#else
#define TRIM_PRINTF(fmt, args) __attribute__((format(__printf__, fmt, args)))
#endif
#else
#define TRIM_PRINTF(fmt, args)
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define HINT_PAR "prefer trim par \"a\" \"b\" ..."
#define HINT_CTX                                                               \
    "prefer trim context -- one call for git status, diff, files, "            \
    "outlines, refs, history; trim context --query <pattern> "                 \
    "<path> for targeted symbol context"
#define HINT_RG "capped -- try trim outline <file>"
#define HINT_OUT "capped -- try trim rg <pat> <file>"

extern size_t MAX_CHARS;
extern size_t MAX_LINES;
extern int HUMAN;

/* util.c -- config + text processing */
void init_max_chars(void);
const char *pick_hint(const char *cmd);
const char *pick_hint_ctx(const char *cmd);
int is_read(const char *s);
int is_lines(const char *s);
void cap(const char *s, int truncated, size_t total_chars, const char *hint);
size_t strip_ansi(char *s, size_t n);
size_t collapse_blanks(char *s, size_t n);
size_t normalize_newlines(char *s, size_t n);
size_t collapse_escapes(char *s, size_t n);
size_t lstrip(char *s);
size_t lstrip_lines(char *s, size_t n);
size_t collapse_spaces(char *s, size_t n);
size_t rtrim_lines(char *s, size_t n);
int is_blank_line(const char *s);

/* exec.c -- command execution */
int run_cmd_str(const char *cmd);
void run_cmd(const char *const *args, int argc);
char *run_capture(const char *cmd);
char *run_cmd_capture(const char *const *args, int argc);
char *run_capture_raw(const char *cmd);
char *run_cmd_capture_raw(const char *const *args, int argc);
void cmd_par(int argc, const char *const *argv);

/* compact.c -- lossless token compaction for search output */
char *compact_paths(const char *s, size_t *out_n);
void cmd_search(const char *const *args, int argc);

/* read.c -- file reading + git helpers */
void read_smart(const char *path);
void read_lines(const char *path, int start, int end);
void cmd_outline(int argc, const char *const *argv);
void cmd_diff(const char *file);
void cmd_blame(const char *file);
void cmd_log(int argc, const char *const *argv);

/* context.c -- enriched, single-call workspace context */
void cmd_context(int argc, const char *const *argv);

/* ref.c -- call-tree references via ast-grep (+ rg fallback) */
void cmd_ref(int argc, const char *const *argv);

/* string.c -- string -> bound key -> translations + call sites (+ rg) */
void cmd_string(int argc, const char *const *argv);

/* string.c -- binding resolvers + rg helpers shared with keyword.c */
extern const char *const RES_GLOBS[9];
extern const char *const CODE_GLOBS[12];
char *argv_to_utf8(const char *s);
int is_resource(const char *path);
void trim_content(const char *content);
int parse_rg_line(const char *s, char *file, size_t fcap, long *line,
                  const char **content);
char *extract_key(const char *content, const char *needle);
char *ident_before(const char *b, const char *cut);
char *define_key(const char *s);
char *bind_key(const char *s, const char *needle);
char *code_key(const char *file, long line, const char *content,
               const char *needle);
char *run_rg(const char *pattern, const char *path, int words,
             const char *const *globs, int nglobs);

/* keyword.c -- fuzzy keyword -> symbols/strings -> refs + callers (xref) */
void cmd_keyword(int argc, const char *const *argv);

#endif
