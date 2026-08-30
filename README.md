# trim

A capped-output wrapper around any command, built for coding agents rather than humans.

## Why

`trim` caps each command's output so the context window stays lean over long agent runs. A human developer doesn't read an entire file before fixing a bug — a coding agent shouldn't either.

**It is not free.** Capping lowers *per-step* token cost, but it does so by prompting more, smaller tool calls, which raises the total step count — and with it, session time and overall cost. Each step is an API round-trip that re-reads the whole growing context from cache, so a longer session compounds the cost. See the [benchmark](#benchmark) below: at equal accuracy, vanilla OpenCode is the cheapest and fastest. `trim` and [rtk](https://github.com/rtk-ai/rtk) are both more expensive on purpose — they buy discipline, not savings.

**The core concept is fewer steps, not smaller steps.** Cutting the cost per step while increasing the step count still raises total cost, because the extra round-trips and cache reads outweigh the fresh-token saving. The way to win is to do more per step: batch independent operations into one call (`trim par`), read a whole small file in one step, and let `trim read` collapse outline + preview into a single call on large files.

If you need unrestricted browsing, run commands directly. This tool is for coding agents.

## Usage

```
trim rg <args>              run ripgrep
trim sg <args>              run ast-grep
trim fd <args>              run fd
trim read|cat|print <file>  smart read (small → whole file; large → outline + preview + hint)
trim lines <file> <s> [<e>] exact lines s..e ($ = EOF)
trim outline <file>         function/class signatures (ast-grep)
trim diff [<file>]          git diff (read-only)
trim blame <file>           git blame (read-only)
trim log [<args>]           git log (read-only)
trim par "cmd1" "cmd2" ...   batch commands into one step — the primary cost saver
trim <command> [args]       run ANY command, output capped
```

Any command not listed above runs as-is with capped output. `trm` is a shorter alias for `trim`.

```
TRIM_MAX_CHARS=500 trim rg pattern          # per-command output cap
TRIM_MAX_LINES=1024 trim lines file 1 40    # range-read line cap (default 512)
```

The "do nothing unless it's too big" rule applies everywhere: `trim read` on a small file prints it whole with no ceremony; `trim lines` returns the exact range untouched and only clamps (at `MAX_LINES`) if the agent asks for something unreasonable like `trim lines file 1 5000`.

## Install

```sh
gcc -O2 -s -o trim trim.c       # Linux/macOS
gcc -O2 -s -o trim.exe trim.c   # Windows (MinGW)
make trim                        # or use the Makefile
```

## PI

Pi agent extensions that enforce the same discipline at the tool level:

| File | Purpose |
|------|---------|
| `enforce-trm.ts` | Auto-prefixes `trim ` to every bash command that doesn't start with it |
| `bash-cap.ts` | Caps ALL bash output at `TRIM_MAX_CHARS`, same format as `trim.c` |
| `APPEND_SYSTEM.md` | Appends trim rules to the system prompt as a text-level reminder |
| `enforce-ask.ts` | Detects agent self-doubt keywords mid-stream and aborts, forcing it to ask the user |

Without these, the model may fall back to built-in tools like `read` or run `cat`/`rg`/`grep` directly, bypassing the capping. The extensions remove those tools and cap all bash output regardless of command.

## Benchmark

12 tasks from SWE-bench Lite (`deepseek/deepseek-v4-flash`, one session per arm, offline). Repro: `benchmark/run_bench.py`.

| arm | time(s) | steps | fresh tokens | cache (M) | cost$ | gold_touched |
|-----|---------|-------|--------------|-----------|-------|--------------|
| **vanilla** | **1,281** | **141** | **169,271** | **23.5** | **0.1184** | 12/12 |
| rtk | 1,528 | 159 | 171,347 | 26.1 | 0.1360 | 12/12 |
| trim | 1,879 | 233 | 118,352 | 33.8 | 0.1608 | 12/12 |
| trimrtk | 2,022 | 225 | 126,502 | 30.6 | 0.1416 | 12/12 |

trim does reduce overall tokens — it read **~30% fewer fresh tokens** than vanilla (118k vs 169k, i.e. less content fed to the model). But it paid for that with **more steps → more cache reads** (33.8M vs 23.5M), and cache is where the cost is. So the fresh-token win is real, but it doesn't offset the added cache and round-trips.

The lesson is the step count, not the cap. Every capping arm cut fresh tokens per step (~30-40%) but ran more steps, so all three were slower and pricier than vanilla. trim was +65% steps and +36% cost; rtk (compact per-command rewrite) did better but still lost. Both trim and rtk optimize cost *per command*; neither cuts the number of steps, which is what dominates total cost and wall time. The goal now is to do more per step — `trim par` batching, whole-small-file reads, and smart `trim read` — to keep the fresh-token saving *and* pull the step count (and cache) under vanilla's.

### Strengths

- **Reliability & completion:** the uncapped arm sometimes burned context and died mid-session; the capped arms finished all 12 every time.
- **Stays local & safe:** capping avoided network/host-package detours (curl, reading `site-packages`) that uncapped arms attempted.
- **Defensive vs huge files:** every read is bounded, so an accidental read of a 1.5 MB file costs a rounding error instead of blowing the context window.

### Weaknesses

- **More steps → more cost and time:** trim +92 steps vs vanilla; each extra step re-reads the growing context and adds a round-trip.
- **No accuracy edge measured:** all arms touched all 12 gold files; the proxy can't separate correct fixes, and real test-pass was not run.
- **Model-dependent:** if the model already searches well (uses `rg`, avoids whole-file dumps), there's little waste to remove; trim pays off most on undisciplined models and long sessions.

### Example: a huge single file

The benchmark's sympy repo ships a 1.5 MB file:

```
task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py  → 1,511,293 bytes
```

**Naive:** `read <file>` → 1,511,293 chars ≈ 377,823 tokens — one file blows a 200k context window by itself.

**trim:** the same one step, capped:

```
$ trim read task05/sympy/integrals/rubi/rubi_tests/tests/test_trinomials.py
    32: def test_1()   1789: def test_2()   2104: def test_3() ...
    [LARGE 3200 lines] use trim lines <file> <start> <end> to read a range
→ ~300 chars — reveals it's 5 test stubs, no need to read the file
```

Same step count as the naive read, ~1000x fewer tokens. This matters because the agent can't know the file is huge until it reads it — trim's every-read-is-bounded behavior turns an accidental read of a giant file into a rounding error, and large files collapse to outline + preview. The bigger the repo, the more often this accident would happen, so the more the safety matters.

## License

MIT
