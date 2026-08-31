# trim

A capped-output wrapper around any command, built for coding agents rather than humans.

## Why

`trim` caps each command's output so the context window stays lean over long agent runs. A human developer doesn't read an entire file before fixing a bug — a coding agent shouldn't either.

**Naive capping is not free.** Simply truncating each command's output lowers per-step tokens but pushes the agent to issue more, smaller calls — more steps means more round-trips and cache re-reads, which can end up slower and pricier than reading whole. So the win isn't the cap; it's **how** trim limits output.

**The design that works: compact search, smart read, cap as a safety net.** `trim rg`/`fd`/`sg` compress the repeated path root into `$1` and show *all* matches in one call (no follow-up queries), and smart `trim read` collapses large files to outline + first/last 10 lines. That cuts fresh tokens *and* steps at the same time — see the [benchmark](#benchmark): trim now beats vanilla on steps, time, cost, and tokens at equal accuracy.

**The core concept is fewer steps, not smaller steps.** Cutting cost per step while increasing step count still raises total cost, because extra round-trips and cache reads outweigh the fresh-token saving. Compaction achieves both at once: less output per call without forcing more calls.

If you need unrestricted browsing, run commands directly. This tool is for coding agents.

## Usage

```
trim rg <args>              run ripgrep (path-compacted)
trim sg <args>              run ast-grep (path-compacted)
trim fd <args>              run fd (path-compacted)
trim read|cat|print <file>  smart read (small → whole; large → outline + first/last 10 + hint)
trim lines <file> <s> [<e>] exact lines s..e ($ = EOF)
trim outline <file>         function/class signatures (ast-grep)
trim diff [<file>]          git diff (read-only)
trim blame <file>           git blame (read-only)
trim log [<args>]           git log (read-only)
trim context                enriched one-call workspace context
trim par "cmd1" "cmd2" ...   batch commands into one step — the primary cost saver
trim <command> [args]       run ANY command, output capped
```

`trim rg`, `trim sg`, and `trim fd` don't just cap — they **compact**: repeated substrings are factored out into reference refs `$1`..`$5` (lossless), so a search's repeated path text collapses without losing any matches. Example:

```
$ trim rg -n "fn check_pen" src/armor_viewer/
$1 = src/armor_viewer/
$1penetration.rs:42:pub fn check_penetration(...)
$1common.rs:457:pub(crate) fn simulate_ap_shell(...)
```

The same compaction applies to `fd` (file lists) and `ast-grep` (structured matches). Capping remains only as a high safety net (8 KB) so a genuinely huge result still can't blow the window. The more matches share path prefixes, the bigger the win — rg/fd output can collapse by an order of magnitude or more.

Any command not listed above runs as-is with capped output. `trm` is a shorter alias for `trim`.

```
TRIM_MAX_CHARS=500 trim rg pattern          # per-command output cap (default 5120)
TRIM_MAX_CHARS=8192 trim lines file 1 40    # range-read char cap (default 5120)
```

The "do nothing unless it's too big" rule applies everywhere: `trim read` on a small file prints it whole with no ceremony; a large file returns outline + first/last 10 lines + a pointer; `trim lines` returns the exact range untouched and only clamps (at `MAX_CHARS`) if the agent asks for something unreasonable like `trim lines file 1 5000`.

## Build

The sources are split across `src/` (`main.c`, `util.c`, `exec.c`, `compact.c`, `read.c`, `context.c`, `trim.h`). Builds are strict — every `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wwrite-strings -Wstrict-prototypes` warning is fatal (`-Werror`):

```sh
make trim            # Linux/macOS; use `make trim.exe` on Windows (MinGW)
# or directly:
gcc -O2 -s -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wwrite-strings \
    -Wstrict-prototypes -Werror -o trim src/main.c src/util.c src/exec.c \
    src/compact.c src/read.c src/context.c

make format          # clang-format all src files (uses .clang-format, 4-space)
```

## PI

Pi agent extensions that enforce the same discipline at the tool level:

| File | Purpose |
|------|---------|
| `enforce-trm.ts` | Auto-prefixes `trim ` to every bash command that doesn't start with it |
| `bash-cap.ts` | Caps ALL bash output at `TRIM_MAX_CHARS`, same format as `trim` |
| `APPEND_SYSTEM.md` | Appends trim rules to the system prompt as a text-level reminder |
| `enforce-ask.ts` | Detects agent self-doubt keywords mid-stream and aborts, forcing it to ask the user |

Without these, the model may fall back to built-in tools like `read` or run `cat`/`rg`/`grep` directly, bypassing the capping. The extensions remove those tools and cap all bash output regardless of command.

## Context

`trim context` is the recommended starting point for an agent. It gathers the relevant
workspace evidence in one agent-visible call, then compacts the combined result once:

```sh
trim context              # current Git status, diff, files, outlines, and source
trim context diff         # the current change set and related file context
trim context path/to/file # file metadata, outline, diff, history, and source windows
trim context --query foo src # matches, references, and related file outlines
```

The bundle includes Git status and diff, changed and untracked files, file metadata,
outlines, source windows, recent history, matches, references, and related outlines.
Large results include an explicit `CONTEXT_TRUNCATED` marker so the agent knows when it
needs a narrower scope.

### Recommended workflow

```sh
trim context                         # understand the current task/change
trim context --query "symbol" src    # find definitions, uses, and related files
trim lines src/file.c 120 180        # exact edit-ready source; preserves whitespace
trim diff src/file.c                 # inspect the final change
```

Use `trim outline <file>` when only the function/class map is needed. Use `trim lines`
for source that will be edited: indentation, blank lines, trailing spaces, and line
boundaries are preserved. Search and context output may use lossless `$1`..`$5`
references for repeated content.

## Benchmark

### Historical: capping alone lost (12-task)

Early cap-only runs (aggressive capping, no compaction). 12 tasks from SWE-bench Lite (`deepseek/deepseek-v4-flash`, one session per arm, offline). Repro: `benchmark/run_bench.py`.

| arm | time(s) | steps | fresh tokens | cache (M) | cost$ | gold_touched |
|-----|---------|-------|--------------|-----------|-------|--------------|
| **vanilla** | **1,281** | **141** | **169,271** | **23.5** | **0.1184** | 12/12 |
| rtk | 1,528 | 159 | 171,347 | 26.1 | 0.1360 | 12/12 |
| trim | 1,879 | 233 | 118,352 | 33.8 | 0.1608 | 12/12 |
| trimrtk | 2,022 | 225 | 126,502 | 30.6 | 0.1416 | 12/12 |

The lesson of the old design: **the step count is the real cost, not the cap.** Every capping arm cut fresh tokens per step but ran more steps, so all three were slower and pricier than vanilla. `trim rg` capped at ~1000 chars forced the agent to re-issue narrow follow-up searches — that drove the 64-vs-10 rg-call gap and the inflated step count. Capping search output was the wrong lever.

### Latest: path compaction + smart read wins (4-task)

The breakthrough is **compacting search output instead of capping it**: `trim rg`/`fd`/`sg` factor out the common directory root into `$1` and show *all* matches in one call (no truncation-driven follow-ups), while smart `trim read` returns outline + first/last 10 lines for large files. Caps stay as a safety net only (`MAX_CHARS`=5120, `MAX_LINES`=512). Same model, 4-task subset, offline.

| arm | time(s) | steps | fresh tokens | cache (M) | cost$ | gold |
|-----|---------|-------|--------------|-----------|-------|------|
| vanilla | 819 | 119 | 99,898 | 11.1 | 0.0659 | 4/4 |
| **trim** | **622** | **98** | **77,438** | **8.2** | **0.0495** | 4/4 |

With compaction, trim beats vanilla on **every** metric — **−18% steps, −24% time, −25% cost, −22% fresh tokens, −26% cache** — at equal accuracy. This is the "fewer, fatter steps" thesis realized: fewer fresh tokens *and* fewer steps, because compacted search shows more per call without re-querying. The win is consistent (repeated across runs); treat exact margins as approximate since the subset runs aren't perfectly isolated.

### Strengths

- **Reliability & completion:** the uncapped arm sometimes burned context and died mid-session; the capped arms finished all 12 every time.
- **Stays local & safe:** capping avoided network/host-package detours (curl, reading `site-packages`) that uncapped arms attempted.
- **Defensive vs huge files:** every read is bounded, so an accidental read of a 1.5 MB file costs a rounding error instead of blowing the context window.

### Weaknesses

- **No accuracy edge measured:** gold-touched is a proxy; it can't separate correct fixes, and real test-pass was not run.
- **Model-dependent:** if the model already searches well (uses `rg`, avoids whole-file dumps), there's little waste to remove; compaction pays off most on undisciplined models and long sessions.
- **Single-run noise / imperfect isolation:** the subset runs aren't perfectly isolated (arms can wander into extra task dirs), so treat the margins as approximate, not a precise verdict.

### Example: a huge single file

The benchmark's sympy repo ships a 1.5 MB file:

```
task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py  → 1,511,293 bytes
```

**Naive:** `read <file>` → 1,511,293 chars ≈ 377,823 tokens — one file blows a 200k context window by itself.

**trim:** the same one step, but smart `trim read` collapses the huge file to outline + first/last 10 lines:

```
$ trim read task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py
    32: def test_1()   1789: def test_2()   2104: def test_3() ...
    ... (skipped) ...
    3200: def test_5()
    [LARGE 3200 lines] use trim lines <file> <start> <end> to read a range
→ ~300 chars — reveals it's 5 test stubs, no need to read the file
```

Same step count as the naive read, ~1000x fewer tokens. This matters because the agent can't know the file is huge until it reads it — smart `trim read` turns an accidental read of a giant file into a rounding error instead of blowing the window. The bigger the repo, the more often this accident would happen, so the more the safety matters.

## License

MIT
