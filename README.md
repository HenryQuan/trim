# trim

Capped-output wrapper around any command — designed for coding agents, not humans.

## Why

Useful for bug fixing and code search, but intentionally limits information to save tokens and avoid context degradation during long agent runs. A human developer doesn't read an entire file before fixing a bug, coding agents shouldn't either.

`trim` caps output at 1000 characters per call, forcing agents to search precisely instead of dumping entire files. This saves tokens, reduces noise, and keeps the context window focused over long sessions. Before capping, `trim` strips ANSI color codes and collapses runs of blank lines, so the 1000-character budget shows real content, not invisible bytes.

If you need unrestricted browsing, run commands directly. This tool is built for coding agents.

## Usage

```
trim rg <args>              run ripgrep
trim sg <args>              run ast-grep
trim fd <args>              run fd
trim outline <file>         extract function/class signatures (ast-grep)
trim diff [<file>]          git diff (read-only)
trim blame <file>           git blame (read-only)
trim log [<args>]           git log (read-only)
trim par "cmd1" "cmd2" ...   batch commands, each output capped separately
trim <command> [args]       run ANY command, output capped
```

`trim` is a generic wrapper: any command not listed above is run as-is with capped output. Shorthands are convenience mappings — `trim diff` becomes `git diff`, `trim sg` becomes `ast-grep`, and so on. `trm` is a shorter alias for `trim`.

```
TRIM_MAX_CHARS=500 trim rg pattern
```

## Install

### Binary (C, ~25 KB)

Download from [releases](https://github.com/henryquan/trim/releases) or build:

```sh
gcc -O2 -s -o trim trim.c       # Linux/macOS
gcc -O2 -s -o trim.exe trim.c   # Windows (MinGW)
make trim                        # or use the Makefile
```

## PI

Pi agent extensions that enforce the same discipline at the tool level:

| File | Purpose |
|------|---------|
| `enforce-trm.ts` | Auto-prefixes `trim ` to every bash command that doesn't start with it — nothing is blocked |
| `bash-cap.ts` | Caps ALL bash output at `TRIM_MAX_CHARS` (default 1000), same format as `trim.c` |
| `APPEND_SYSTEM.md` | Appends trim rules to system prompt as a text-level reminder |
| `enforce-ask.ts` | Detects agent self-doubt keywords mid-stream and aborts — forces it to ask the user |

Without these, the model may still use built-in tools like `read` or run `cat`/`rg`/`grep` directly, bypassing `trim`'s capping. The extensions remove the tools entirely and cap all bash output regardless of command.

## Benchmark

Measured on 12 tasks from SWE-bench Lite (`deepseek/deepseek-v4-flash`, one session per arm, henry-guide skill + trim vs no skill + no trim, offline). Repro: `benchmark/run_bench.py`.

Findings are mixed — trim is not a free win.

### Strengths

- **Per-step token efficiency (the real metric):** trim added ~1,215 fresh tokens/step vs ~1,783 without (~32% less), and kept average context leaner (156k vs 169k). Capped output → smaller context growth.
- **Compounds over long sessions:** leaner context per step matters more the longer the run. Per-step saving (~30-50%) is diluted in short runs because extra search steps add up.
- **Completion reliability:** in one run the undisciplined arm burned tokens and died mid-session (an auto-rejected external read); the trim arm finished all 12 every time.
- **Stays local:** enforced trim+skill avoided the network/host-package lookup detours (curl, reading `site-packages`); the bare arm attempted both.

### Weaknesses

- **Slower wall time:** +67% in our fair run (2,135s vs 1,281s). Precise search costs more round-trips.
- **Higher cost:** $0.175 vs $0.118 (more requests, more cache reads outweigh the fresh-token saving).
- **Modest total saving:** only ~8% fewer fresh tokens over a fixed 12-task workload, because the extra steps dilute the per-step win.
- **No accuracy edge measured:** both arms touched all 12 gold files — the proxy can't separate correct fixes, and real test-pass was not run.
- **Model-dependent:** if the baseline model already searches well (uses `rg`, avoids whole-file dumps), trim has little waste left to remove. It pays off most on models that dump files, and on long sessions.

Bottom line: trim trades wall time and money for per-step token efficiency and reliability. Worth it for long sessions and undisciplined models; marginal for short runs where the model already searches precisely.

### Example: searching a large multi-repo workspace

Real case from the benchmark setup (12 repos, and sympy ships a 1.5 MB single file):

```
task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py  → 1,511,293 bytes
```

**Naive (no trim):**

```
read task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py
→ 1,511,293 chars ≈ 377,823 tokens   # one file, blows a 200k window by itself
```

**trim:**

```
$ trim rg -n "def uniq" task05/sympy
task05/sympy\core\symbol.py:124:def uniquely_named_symbol(...)
task05/sympy\utilities\_compilation\util.py:276:def unique_list(l)
task05/sympy\utilities\iterables.py:2077:def uniq(seq, result=None):
task05/sympy\benchmarks\bench_symbench.py:44:def uniq(x)
→ ~300 chars

$ trim outline task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py
   32: def test_1():   1789: def test_2():   2104: def test_3(): ...
→ ~200 chars — reveals it's 5 test stubs, no need to read the file

$ trim sed task05/sympy/utilities/iterables.py 2077 2090
→ the exact lines needed
```

A few hundred tokens vs ~378k for one naive read — roughly **1000x** on that file alone. The win multiplies because every wasted read pollutes all later steps. In a 30-repo knowledge base the danger is whole-file reads during exploration; search output (paths + line numbers) is naturally tiny, so trim's cap keeps the whole session lean.

The key is that trim is **defensive**: the agent has no idea `test_trinomials.py` is 1.5 MB until it reads it. Even a careful model that usually searches precisely can open a huge file by accident (and context-capped agents like Codex/Claude then hit the cutoff, or burn 30k+ tokens on the file alone). trim removes that failure mode — *every* read is capped at 1000 chars, so even an accidental read of a giant file costs a rounding error. The bigger and more numerous the files, the more often this accident happens, so **the larger the repo, the more trim shines**. It's insurance against the read you didn't know was expensive.

Caveat: this only works if the model actually uses `trim` — the PI extensions (`enforce-trm.ts`, `bash-cap.ts`) guarantee it at the tool level; prompt-only discipline can be bypassed (bare `read`, `cat`, `curl`).

## License

MIT
