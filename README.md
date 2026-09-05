# trim

A capped-output wrapper around any command — a context-discipline layer for coding agents, with xref commands (`context`, `ref`, `string`, `keyword`) that serve humans and agents alike.

## Why

Vanilla tooling is amazing. For most interactive work, plain `rg`, `cat`, and direct shell are the right choice — don't add a wrapper you don't need.

`trim` is for a narrow case: long agent sessions where the context window is the bottleneck. A human doesn't read an entire file before fixing a bug — an agent shouldn't either. Naive capping backfires there: it forces more, smaller calls, and the extra steps cost more than the saved tokens. So trim compacts instead — search results are factored losslessly and reads collapse to outlines — cutting fresh tokens *and* step count at once (see [Benchmark](#benchmark)).

## Usage

Two layers:

**Compaction layer** — agent context discipline; every output is capped and losslessly compacted:

```
trim rg <args>                    run ripgrep (path-compacted)
trim sg <args>                    run ast-grep (path-compacted)
trim fd <args>                    run fd (path-compacted)
trim read|cat|print <file>        smart read (small → whole; large → outline + first/last 10 + hint)
trim lines <file> <s> [<e>]       exact lines s..e, edit-ready ($ = EOF)
trim outline <file>               function/class signatures (ast-grep)
trim diff [<file>]                git diff (read-only)
trim blame <file>                 git blame (read-only)
trim log [<args>]                 git log (read-only)
trim par "cmd1" "cmd2" ...        batch commands into one step — the primary cost saver
trim <command> [args]             run ANY command, output capped
```

**Xref layer** — fewer steps, more info: each command answers a real question in one call and returns dense related info only (no raw dumps). Built for human and agent use alike:

```
trim context                      one-call workspace context (status, diff, files, outlines, history)
trim ref <symbol> [path] [--depth N]
                                  syntax call tree: callees + callers by file/function/line
trim string <text> [path]         string -> bound key -> translations + call sites
trim keyword <kw...> [path] [--depth N]
                                  fuzzy keywords -> symbols + string-bound ids -> refs + callers
```

`trm` is a shorter alias. Any command not listed above runs as-is with capped output.

External tools each command shells out to (everything else is built in — `lines`, `par`, and the generic cap need none):

| needs | commands |
|-------|----------|
| `rg` | `rg`, `string`, `keyword` (references + string literals), `ref` (fallback), `context` |
| `ast-grep` | `sg`, `outline`, `read` (large-file outlines), `context` (outlines); `ref`/`keyword` use it for the syntax index and degrade to text-level `rg` without it |
| `git` | `diff`, `blame`, `log`, `context` |
| `fd` | `fd` |

### Search compaction

`trim rg`/`sg`/`fd` don't just cap — they compact: repeated substrings are factored into lossless refs `$1`..`$5`, so all matches fit in one call with no follow-up queries:

```
$ trim rg -n "fn check_pen" src/armor_viewer/
$1 = src/armor_viewer/
$1penetration.rs:42:pub fn check_penetration(...)
$1common.rs:457:pub(crate) fn simulate_ap_shell(...)
```

Capping remains only as a safety net so a genuinely huge result can't blow the window.

```sh
TRIM_MAX_CHARS=500 trim rg pattern     # per-command output cap (default 5120)
TRIM_HUMAN=1 trim rg pattern           # disable compaction globally (raw output)
```

Terminal output is automatically paged through `TRIM_PAGER`, then `PAGER`.
The default is `less -FRX` on macOS/Linux (requires `less`) and `more` on Windows.
The Unix default exits automatically for short output, preserves ANSI colors,
and leaves output in the terminal. Paging shows uncompressed output with raised
limits. Set `TRIM_NO_PAGER=1` to disable it; piped output never uses a pager.

### Reading files

Smart `trim read` turns an accidental read of a giant file into a rounding error — a 1.5 MB file collapses to an outline plus first/last lines:

```
$ trim read task05/sympy/.../test_trinomials.py
    32: def test_1()   1789: def test_2()   2104: def test_3() ...
    [LARGE 3200 lines] use trim lines <file> <start> <end> to read a range
```

`trim lines` returns the exact range untouched — indentation, blank lines, trailing spaces, and line boundaries are preserved — use it for source you will edit. `trim outline` when only the function/class map is needed.

### Call tree

`trim ref <symbol> [path] [--depth N]` answers both "what does this call?" and "what calls this?" in one step:

```
$ trim ref _getAnimeList lib/
CALLS OUT
_getAnimeList
  lib/core/GlobalData.dart:390  downloadHTML  |  parser.downloadHTML()
  lib/core/GlobalData.dart:393  parseHTML    |  parser.parseHTML(doc)
CALLED BY
_getAnimeList
  lib/core/GlobalData.dart:328  init  |  _getAnimeList()
```

Each section is traversed breadth-first to `--depth` (default 2, max 8), so one call walks the neighborhood of a symbol — a callee's callees, a caller's callers. The engine is `src/ref/`: one `ast-grep run --kind` pass per language profile (18 languages) builds a callee → call-sites index; when ast-grep is missing or nothing matches, it falls back to plain `rg`.

Resolution is syntax-level, by name: no type system, so same-named functions merge (narrow `path` to disambiguate). Root matching has two fuzzy modes — traversal edges stay exact, only the starting roots are fuzzy:

```
trim ref HistoryGroup lib/ --sub     # substring roots, smartcase (all-lowercase = case-insensitive)
trim ref '^_?get[A-Z]' lib/ --re     # regex roots (tiny built-in engine: ^ $ . * + ? [a-z] \d \w \s)
```

### String & keyword xref

`trim string <text> [path]` is string → xref: find the UI string, resolve the identifier bound to it, then report translations and every code reference with its enclosing function. Bindings are resolved from i18n resources (`.arb`, `.json`, `.yaml`, `.xml`, `.strings`, `.properties`, `.po`, `.resx`), C/C++ `#define` (including `\` continuations), and same-line constants — `IDENT = "lit"`, `IDENT : type = "lit"` — in C++, Rust, Zig, Kotlin, Swift, Dart/Flutter, Go, JS/TS, Python. Passing the key itself works too:

```
$ trim string "prefer trim context"
1 LITERAL
  src/trim.h:21:    "prefer trim context -- one call for git status, diff, files, "
2 KEY
  HINT_CTX
3 TRANSLATIONS
  (none)
4 CALL SITES
  src/util.c:59  pick_hint_ctx  |  return HINT_CTX;
  src/trim.h:20  ?  |  #define HINT_CTX
```

`trim keyword <kw...> [path] [--depth N]` starts from fuzzy memory instead: keywords match any symbol or string-bound identifier, seeds are ranked by how many keywords they hit, then every reference is annotated with its enclosing function and callers are walked up the call graph (BFS to `--depth`, default 2, max 8; up to 12 keywords; the last positional arg is the path only if it exists on disk). "load anime list" finds `_getAnimeList` even though it contains neither keyword `load` nor a literal `load anime list`:

```
$ trim keyword load anime list animeone/lib
[KEYWORD v1] kws=load,anime,list path=animeone/lib depth=2
SEEDS
  AnimeList      (2/3 kws)
  _getAnimeList  (2/3 kws)  lib/core/GlobalData.dart:388
  getAnimeList   (2/3 kws)  lib/core/GlobalData.dart:186
  ...
REFERENCES
  _getAnimeList
  lib/core/GlobalData.dart:328  init  |  await _getAnimeList();
  lib/core/GlobalData.dart:341  init  |  await _getAnimeList();
  lib/core/GlobalData.dart:388  _getAnimeList  |  Future _getAnimeList() async {
CALLERS
  _getAnimeList
  lib/core/GlobalData.dart:328  init  |  _getAnimeList()
  init
  lib/ui/page/home.dart:54  _loadData  |  global.init()
```

Seeds come from the same index as `trim ref` plus an `rg -i` pass over string literals (resolved through the same binders as `trim string`). Output is capped like every other command — narrow the keywords or raise `TRIM_MAX_CHARS` when the seed list floods.

### Context

`trim context` is the recommended starting point: Git status and diff, changed and untracked files, file metadata, outlines, source windows, recent history, and matches — one call, compacted once. Large results carry an explicit `CONTEXT_TRUNCATED` marker so the agent knows to narrow the scope.

```sh
trim context                         # understand the current task/change
trim context diff                    # the current change set and related file context
trim context path/to/file            # file metadata, outline, diff, history, source windows
trim context --query "symbol" src    # find definitions, uses, and related files
```

## Build

The Makefile is the source of truth:

```sh
make trim        # Linux/macOS (make trim.exe on Windows / MinGW)
make format      # clang-format all sources
```

Builds are strict — the full repo warning set is fatal. Sources live in `src/` with the call-tree engine in `src/ref/`.

## Benchmark

Expected verdict: vanilla wins on disciplined models and short tasks; trim wins on long, wandering sessions — that's the use case it's built for. Repro: `benchmark/run_bench.py` (SWE-bench Lite subset, one session per arm, offline).

| arm | time(s) | steps | fresh tokens | cache (M) | cost$ | gold |
|-----|---------|-------|--------------|-----------|-------|------|
| vanilla | 819 | 119 | 99,898 | 11.1 | 0.0659 | 4/4 |
| **trim** | **622** | **98** | **77,438** | **8.2** | **0.0495** | 4/4 |

On that use case, trim beats vanilla on every metric — **−18% steps, −24% time, −25% cost, −22% fresh tokens** — at equal accuracy. An earlier cap-only design (no compaction) lost on all metrics; the lesson drove the current design: compact the output, don't just truncate it. Full history: `benchmark/swe-bench-lite-12.md`.

## License

MIT
