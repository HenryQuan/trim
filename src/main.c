#include "trim.h"

int _CRT_glob = 0;

static const char *HELP =
    "trim: capped-output wrapper around any command\n"
    "\n"
    "Always use trim (alias trm) instead of raw commands -- output is capped "
    "to save tokens.\n"
    "Follow understand -> ask -> inspect -> simplify -> change -> verify -> "
    "report\n"
    "\n"
    "Usage:\n"
    " trim context [diff|--query <pattern>] [path] enriched one-call context\n"
    "   context includes status, diff, files, outlines, references, history, "
    "and source\n"
    "   start with: trim context; exact edits: trim lines <file> <start> "
    "<end>\n"
    "  trim <command> [args]          run any command, output capped\n"
    "  trim rg <args>                 run ripgrep\n"
    "  trim sg <args>                 run ast-grep\n"
    "  trim fd <args>                 run fd\n"
    "  trim lines <file> <start> [<end>]  exact edit-ready lines; spaces "
    "preserved\n"
    "    aliases: l/sed; $ = EOF\n"
    "  trim read|cat|print <file>     read whole file with output cap (prefer "
    "trim lines)\n"
    "  trim outline <file>            extract function/class signatures "
    "(ast-grep)\n"
    "  trim diff [<file>]             git diff (read-only)\n"
    "  trim blame <file>              git blame (read-only)\n"
    "  trim log [<args>]              git log (read-only)\n"
    "  trim par \"cmd1\" \"cmd2\" ...    batch commands, each output capped\n"
    "  trim read/lines output is capped at MAX_CHARS (default 5120, env "
    "TRIM_MAX_CHARS);\n"
    "  prefer trim lines over trim read/cat/print to avoid whole-file reads.\n";

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
