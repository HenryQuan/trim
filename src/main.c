#include "trim.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

int _CRT_glob = 0;

/* Page terminal output; captured output keeps its normal limits. */
static FILE *pager_fp;

static void close_pager(void) {
    if (!pager_fp)
        return;
    fflush(stdout);
    /* fd 1 must stop referencing the pipe or the pager never sees EOF and
       _pclose deadlocks; point it at a bit bucket, then close the stream */
#ifdef _WIN32
    int nul = _open("NUL", _O_WRONLY);
    if (nul >= 0) {
        _dup2(nul, 1);
        _close(nul);
    } else {
        _close(1);
    }
    _pclose(pager_fp);
#else
    int nul = open("/dev/null", O_WRONLY);
    if (nul >= 0) {
        dup2(nul, 1);
        close(nul);
    } else {
        close(1);
    }
    pclose(pager_fp);
#endif
    pager_fp = NULL;
}

static void setup_pager(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdout)))
        return;
#else
    if (!isatty(fileno(stdout)))
        return;
#endif
    if (getenv("TRIM_NO_PAGER"))
        return;
    const char *pg = getenv("TRIM_PAGER");
    if (!pg || !*pg)
        pg = getenv("PAGER");
    if (!pg || !*pg) {
#ifdef _WIN32
        pg = "more";
#else
        pg = "less -FRX";
#endif
    }
#ifdef _WIN32
    pager_fp = _popen(pg, "w");
#else
    pager_fp = popen(pg, "w");
#endif
    if (!pager_fp)
        return;
#ifdef _WIN32
    _dup2(_fileno(pager_fp), 1);
#else
    dup2(fileno(pager_fp), 1);
#endif
    atexit(close_pager);
    /* human at a terminal: print everything, no compaction */
    MAX_CHARS = (size_t)1 << 30;
    MAX_LINES = 1 << 20;
    HUMAN = 1;
}

static const char *HELP =
    "trim: smart, capped-output coding wrapper\n"
    "\n"
    "WORKFLOW\n"
    "  understand -> ask -> inspect -> simplify -> change -> verify -> report\n"
    "\n"
    "RULES\n"
    "  Literal request only. Do not infer extra work.\n"
    "  Unclear / missing / conflicting -> ask ONE question and stop.\n"
    "  Unsure -> \"I am not sure.\" and stop.\n"
    "  Never invent inputs, assumptions, credentials, or workarounds.\n"
    "  Inspect before changing; search first, read only what is needed.\n"
    "  Prefer keyword/ref/string/context; use par for independent work.\n"
    "  Simplest correct change; reuse existing patterns; fix root causes.\n"
    "  No unrequested fixes, tests, cleanup, refactors, or optimization.\n"
    "  Verify the smallest relevant check; never claim success without evidence.\n"
    "  Report: changed / skipped / verified. <=3 lines.\n"
    "\n"
    "USAGE\n"
    "  trim <command> [args...]       Run command with capped output\n"
    "  trim context [mode] [path]     Build smart workspace context\n"
    "  trim par \"cmd1\" \"cmd2\" ...    Run independent commands in parallel\n"
    "\n"
    "DISCOVERY - PREFER THESE\n"
    "  trim keyword <term...> [path]  Semantic search: symbols, strings, refs, callers\n"
    "  trim ref <symbol> [path]       AST callers + callees (--depth N)\n"
    "  trim string <text> [path]      Trace text/i18n -> key -> translations -> calls\n"
    "  trim outline <file>            Functions, types, impls, declarations\n"
    "  trim sg <pattern> [path]       Structural AST-grep search\n"
    "  trim rg <args...>              Raw regex/glob fallback\n"
    "  trim fd [args...]              File discovery fallback\n"
    "\n"
    "INSPECTION\n"
    "  trim read <file>               Smart, capped file inspection\n"
    "  trim lines <file> <start> [end] Exact edit-ready source lines; $ = EOF\n"
    "  trim cat/print <file>          Aliases for read\n"
    "\n"
    "GIT / CONTEXT\n"
    "  trim context                    Status, diff, files, relevant code\n"
    "  trim context diff [path]        Context focused on current diff\n"
    "  trim context --query <term>     Context around term/symbol\n"
    "  trim diff [file]                Read-only git diff\n"
    "  trim blame <file>               Read-only git blame\n"
    "  trim log [git args...]          Read-only git log\n"
    "\n"
    "PREFERRED FLOW\n"
    "  context -> keyword/fd -> ref/string/outline/sg -> read/lines -> change -> diff\n"
    "  Use par aggressively for independent searches and inspections.\n"
    "  Prefer semantic/AST discovery over raw search; targeted lines over file dumps.\n"
    "\n"
    "OUTPUT\n"
    "  Output is capped at MAX_CHARS (default: 5120).\n"
    "  TRIM_MAX_CHARS=<n> trim <command>\n"
    "  TRIM_MAX_LINES=<n> trim <command>\n";

int main(int argc, char **argv) {
    init_max_chars();
#ifdef _WIN32
    /* set the console codepage before spawning the pager: more and other
       children write bytes that the console decodes with this codepage */
    SetConsoleOutputCP(CP_UTF8);
#endif
    setup_pager();
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(HELP, stdout);
        return 0;
    }

    char *cmd = argv[1];

    if (is_read(cmd)) {
        if (argc < 3) {
            fprintf(stderr, "error: missing file path\n");
            return 1;
        }
        read_smart(argv[2]);
        return 0;
    }

    if (is_lines(cmd)) {
        if (argc < 4) {
            fprintf(stderr,
                    "error: usage: trim lines <file> <start> [<end>]\n");
            return 1;
        }
        int start = atoi(argv[3]);
        int end = 0;
        if (argc > 4 && strcmp(argv[4], "$"))
            end = atoi(argv[4]);
        if (start < 1) {
            fprintf(stderr, "error: start line must be >= 1\n");
            return 1;
        }
        if (end > 0 && end < start) {
            fprintf(stderr, "error: end line must be >= start\n");
            return 1;
        }
        read_lines(argv[2], start, end);
        return 0;
    }

    if (!strcmp(cmd, "outline")) {
        if (argc < 3) {
            fprintf(stderr, "error: usage: trim outline <file>\n");
            return 1;
        }
        cmd_outline(argc, (const char *const *)argv);
        return 0;
    }

    if (!strcmp(cmd, "diff")) {
        cmd_diff(argc > 2 ? argv[2] : NULL);
        return 0;
    }

    if (!strcmp(cmd, "blame")) {
        if (argc < 3) {
            fprintf(stderr, "error: usage: trim blame <file>\n");
            return 1;
        }
        cmd_blame(argv[2]);
        return 0;
    }

    if (!strcmp(cmd, "log")) {
        cmd_log(argc, (const char *const *)argv);
        return 0;
    }

    if (!strcmp(cmd, "par")) {
        if (argc < 3) {
            fprintf(stderr, "error: usage: trim par \"cmd1\" \"cmd2\" ...\n");
            return 1;
        }
        cmd_par(argc - 2, (const char *const *)argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "context")) {
        cmd_context(argc - 2, (const char *const *)argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "ref")) {
        cmd_ref(argc - 2, (const char *const *)argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "string")) {
        cmd_string(argc - 2, (const char *const *)argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "keyword")) {
        cmd_keyword(argc - 2, (const char *const *)argv + 2);
        return 0;
    }

    if (!strcmp(cmd, "rg")) {
        cmd_search((const char *const *)argv + 1, argc - 1);
        return 0;
    }

    if (!strcmp(cmd, "sg") || !strcmp(cmd, "ast-grep")) {
        argv[1] = (char *)"ast-grep";
        cmd_search((const char *const *)argv + 1, argc - 1);
        return 0;
    }

    if (!strcmp(cmd, "fd")) {
        argv[1] = (char *)"fd";
        cmd_search((const char *const *)argv + 1, argc - 1);
        return 0;
    }
    /* anything else: run as-is, output capped */

    run_cmd((const char *const *)argv + 1, argc - 1);
    return 0;
}
