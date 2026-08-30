#ifndef TRIM_H
#define TRIM_H

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

#define HINT_PAR "prefer trim par \"a\" \"b\" ..."
#define HINT_RG  "capped — try trim outline <file>"
#define HINT_OUT "capped — try trim rg <pat> <file>"

extern size_t MAX_CHARS;
extern size_t MAX_LINES;

/* util.c — config + text processing */
void init_max_chars(void);
const char *pick_hint(const char *cmd);
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
int is_blank_line(const char *s);

/* exec.c — command execution */
int run_cmd_str(const char *cmd);
void run_cmd(char **args, int argc);
char *run_capture(const char *cmd);
char *run_cmd_capture(char **args, int argc);
void cmd_par(int argc, char **argv);

/* compact.c — lossless token compaction for search output */
char *compact_paths(const char *s, size_t *out_n);
void cmd_search(char **args, int argc);

/* read.c — file reading + git helpers */
void read_smart(const char *path);
void read_lines(const char *path, int start, int end);
void cmd_outline(int argc, char **argv);
void cmd_diff(const char *file);
void cmd_blame(const char *file);
void cmd_log(int argc, char **argv);

#endif
