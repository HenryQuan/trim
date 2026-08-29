#!/usr/bin/env python3
"""Benchmark: one opencode session per arm over the 12 tasks, then compare.

Arm 1 "trim":   henry-guide skill + trim on PATH (trim rg / trim outline / trim sed / trim diff).
Arm 2 "notrim": no skill, no trim — agent reads files however it likes.

For each arm it records wall time, token usage (from --format json stream) and
accuracy = number of tasks whose gold patch file was modified (proxy).

Usage: uv run benchmark/run_bench.py            # run both arms
       uv run benchmark/run_bench.py --arm trim # run a single arm
       uv run benchmark/run_bench.py --report   # print comparison from saved out/*.json
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

BENCH = Path(__file__).resolve().parent
ROOT = BENCH.parent                       # hybrid-grep root (holds trim.exe)
WORK = BENCH / "work"
OUT = BENCH / "out"
PROMPTS = BENCH / "prompts"
TASKS = json.loads((BENCH / "tasks.json").read_text(encoding="utf-8"))
MODEL = os.environ.get("OPENCODE_MODEL", "deepseek/deepseek-v4-flash")
OFFLINE_CONFIG = BENCH / "opencode.offline.json"

TRIM_PROMPT = (
    "There are 12 bug reports, one per subdirectory task01..task12 (see tasks.md). Fix the bug in "
    "each codebase.\n\n"
    "Rules you MUST follow:\n"
    "1. Load the henry-guide skill and follow it exactly.\n"
    "2. All file inspection MUST go through trim: trim rg, trim outline <file>, trim sed <file> <n>, "
    "trim diff. NEVER read whole files directly.\n"
    "3. You have NO network access and no runtime interpreters: never use curl, wget, webfetch, "
    "python, node, pip, uv, or git fetch/clone/pull. Work only from the local repo files.\n"
    "4. After a change, review your diff (trim diff) instead of running tests — they cannot run here.\n"
    "5. Fix all 12; do not stop early."
)
NOTRIM_PROMPT = (
    "There are 12 bug reports, one per subdirectory task01..task12 (see tasks.md). Fix the bug in "
    "each codebase; do not stop until all 12 are fixed. Use whatever tools or approach you like. "
    "Note: you have NO network access and no runtime interpreters — do not attempt curl, wget, "
    "webfetch, python, node, pip, uv, or git fetch/clone/pull. Work only from the local repo files."
)


def gen_tasks_md() -> None:
    PROMPTS.mkdir(parents=True, exist_ok=True)
    import re
    lines = ["# Bug reports", "", "Fix the bug described in each subdirectory. Do not stop early.", ""]
    for t in TASKS:
        st = re.sub(r"https?://\S+", "[link removed]", t["problem_statement"])
        lines += [f"## {t['dir']}", f"Relevant tests: {', '.join(t['FAIL_TO_PASS'])}", "", st, ""]
    (PROMPTS / "tasks.md").write_text("\n".join(lines), encoding="utf-8")


def reset_all() -> None:
    """Restore every task repo to its base_commit so each arm starts identical and clean."""
    for t in TASKS:
        d = WORK / t["dir"]
        subprocess.run(["git", "-C", str(d), "reset", "-q", "--hard", t["base_commit"]],
                       capture_output=True, text=True)
        subprocess.run(["git", "-C", str(d), "clean", "-q", "-fdx"],
                       capture_output=True, text=True)


def run_arm(arm: str) -> dict:
    gen_tasks_md()
    env = dict(os.environ)
    env["OPENCODE_CONFIG"] = str(OFFLINE_CONFIG)     # deny webfetch/websearch + curl/wget
    if arm == "trim":
        env["PATH"] = str(ROOT) + os.pathsep + env.get("PATH", "")
        prompt = TRIM_PROMPT
    else:
        env["PATH"] = os.pathsep.join(p for p in env.get("PATH", "").split(os.pathsep)
                                      if p and p.rstrip("\\/") != str(ROOT).rstrip("\\/"))
        prompt = NOTRIM_PROMPT

    OUT.mkdir(parents=True, exist_ok=True)
    json_path = OUT / f"{arm}.json"
    err_path = OUT / f"{arm}.err"
    cmd = ["opencode", "run", "--format", "json", "-m", MODEL,
           "--title", f"bench-{arm}", prompt, "-f", str(PROMPTS / "tasks.md")]

    print(f"[{arm}] starting  model={MODEL}  cwd={WORK}")
    start = time.monotonic()
    with open(json_path, "w", encoding="utf-8") as fj, open(err_path, "w", encoding="utf-8") as fe:
        r = subprocess.run(cmd, cwd=WORK, env=env, stdout=fj, stderr=fe, text=True)
    elapsed = time.monotonic() - start
    print(f"[{arm}] finished  exit={r.returncode}  {elapsed:.0f}s  -> {json_path.name} ({json_path.stat().st_size}B)")

    stats = parse_stream(json_path)
    stats["arm"] = arm
    stats["elapsed_s"] = round(elapsed, 1)
    stats["accuracy"] = accuracy()
    (OUT / f"{arm}.result.json").write_text(json.dumps(stats, indent=1), encoding="utf-8")
    return stats


def parse_stream(json_path: Path) -> dict:
    NET_TOOLS = {"webfetch", "websearch"}
    NET_CMDS = ("curl", "wget", "python", "python3", "py", "node", "npm", "npx", "pip", "pip3", "uv", "uvx")
    NET_GIT = ("fetch", "pull", "clone", "ls-remote", "push")

    inp = out = cache = cost = steps = 0
    tools: dict[str, int] = {}
    blocked = 0
    session = ""
    for line in json_path.read_text(encoding="utf-8").splitlines():
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        if o.get("sessionID"):
            session = o["sessionID"]
        if o.get("type") == "step_finish" and o.get("part", {}).get("tokens"):
            t = o["part"]["tokens"]
            steps += 1
            inp += t.get("input", 0)
            out += t.get("output", 0)
            cache += t.get("cache", {}).get("read", 0)
            cost += o.get("part", {}).get("cost", 0) or 0
        if o.get("type") == "tool_use":
            tool = o["part"].get("tool", "?")
            tools[tool] = tools.get(tool, 0) + 1
            if tool in NET_TOOLS:
                blocked += 1
            if tool == "bash":
                cmd = str(o["part"].get("state", {}).get("input", {}).get("command", "")).strip()
                head = cmd.split()
                first = head[0] if head else ""
                if first in NET_CMDS or first in ("curl", "wget"):
                    blocked += 1
                elif first == "trim" and len(head) > 1:
                    sub = head[1]
                    if sub in NET_CMDS or sub == "git" and head[2:3] and head[2] in NET_GIT:
                        blocked += 1
                elif first == "git" and len(head) > 1 and head[1] in NET_GIT:
                    blocked += 1
    return {"session": session, "steps": steps, "input": inp, "output": out,
            "cache_read": cache, "total": inp + out, "cost": round(cost, 6),
            "tools": tools, "blocked": blocked}


def accuracy() -> dict:
    """Per-task: did the agent modify the gold patch file? (proxy for pass)"""
    per = {}
    for t in TASKS:
        d = WORK / t["dir"]
        gold = t["gold_files"]
        base_sha = t["base_commit"]
        head_sha = subprocess.run(["git", "-C", str(d), "rev-parse", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
        # changed anywhere? uncommitted diff or committed past base
        stat = subprocess.run(["git", "-C", str(d), "status", "--porcelain"],
                              capture_output=True, text=True).stdout.strip()
        touched_gold = False
        if stat:
            for f in gold:
                diff = subprocess.run(["git", "-C", str(d), "diff", "--", f],
                                      capture_output=True, text=True).stdout.strip()
                if diff:
                    touched_gold = True
                    break
        elif head_sha != base_sha:            # committed work: diff gold files vs the base commit
            for f in gold:
                diff = subprocess.run(["git", "-C", str(d), "diff", base_sha, "HEAD", "--", f],
                                      capture_output=True, text=True).stdout.strip()
                if diff:
                    touched_gold = True
                    break
        per[t["instance_id"]] = {"any_change": bool(stat) or head_sha != base_sha,
                                 "gold_touched": touched_gold}
    return per


def report(saved: dict) -> None:
    header = (f"{'arm':<7} {'time(s)':>8} {'steps':>6} {'input':>10} {'output':>10} "
              f"{'cache':>10} {'total':>10} {'cost$':>8} {'gold_touched':>13} {'blocked':>8}")
    print(header)
    print("-" * len(header))
    for arm, s in saved.items():
        acc = s.get("accuracy", {})
        n = sum(1 for v in acc.values() if v.get("gold_touched"))
        print(f"{arm:<7} {s['elapsed_s']:>8.0f} {s['steps']:>6} {s['input']:>10} {s['output']:>10} "
              f"{s['cache_read']:>10} {s['total']:>10} {s['cost']:>8.4f} {n}/12 {s.get('blocked', '?'):>8}")
    print("total = input + output (fresh tokens); cache.read billed separately; "
          "accuracy = gold file touched; blocked = attempted network/runtime calls "
          "(webfetch/curl/wget/python/node/pip/uv/git fetch)")


def load_saved() -> dict:
    saved = {}
    for arm in ("trim", "notrim"):
        p = OUT / f"{arm}.result.json"
        if p.exists():
            saved[arm] = json.loads(p.read_text(encoding="utf-8"))
    return saved


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=["trim", "notrim"], help="run a single arm")
    ap.add_argument("--report", action="store_true", help="print comparison from saved results")
    ap.add_argument("--rerun", action="store_true",
                    help="discard saved results and rerun all pending arms")
    args = ap.parse_args()

    if args.report:
        saved = load_saved()
        if not saved:
            sys.exit("no saved results yet — run the benchmark first")
        report(saved)
        return

    saved = load_saved()
    if args.rerun:
        for arm in ("trim", "notrim"):
            (OUT / f"{arm}.result.json").unlink(missing_ok=True)
            (OUT / f"{arm}.json").unlink(missing_ok=True)
        saved = {}
    if not args.arm:
        if len(saved) == 2:
            report(saved)
            return
    arms = [args.arm] if args.arm else [a for a in ("trim", "notrim") if a not in saved]
    for i, arm in enumerate(arms):
        reset_all()
        saved[arm] = run_arm(arm)
    report(saved)


if __name__ == "__main__":
    main()
