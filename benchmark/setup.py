#!/usr/bin/env python3
"""Setup: checkout each of the 12 SWE-bench Lite repos at base_commit under work/<id>/.

Resumable: skips repos already checked out. Needs git on PATH.

Usage: uv run benchmark/setup.py
"""
import json
import subprocess
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent
WORK = BENCH / "work"
TASKS = json.loads((BENCH / "tasks.json").read_text(encoding="utf-8"))


def checkout(task: dict) -> bool:
    d = WORK / task["dir"]
    if (d / ".git").exists():
        print(f"skip  {task['instance_id']} -> {task['dir']}")
        return False
    # migrate dirs created under the old instance-id naming
    old = WORK / task["instance_id"]
    if old.exists() and not d.exists():
        old.rename(d)
        print(f"moved {task['instance_id']} -> {task['dir']}")
        return False
    d.mkdir(parents=True, exist_ok=True)
    cmds = [
        ["git", "init", "-q", str(d)],
        ["git", "-C", str(d), "remote", "add", "origin", task["url"]],
        ["git", "-C", str(d), "fetch", "-q", "--depth", "1", "origin", task["base_commit"]],
        ["git", "-C", str(d), "checkout", "-q", "FETCH_HEAD"],
        ["git", "-C", str(d), "checkout", "-q", "-b", "base"],
    ]
    for cmd in cmds:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"FAIL {task['instance_id']}: {' '.join(cmd)} -> {r.stderr.strip()}")
            sys.exit(1)
    print(f"ok    {task['instance_id']} -> {task['dir']} @ {task['base_commit'][:8]}")
    return True


def strip_remotes() -> None:
    """Remove origin so the agent sees no repo URL / cannot fetch."""
    for task in TASKS:
        d = WORK / task["dir"]
        r = subprocess.run(["git", "-C", str(d), "remote", "remove", "origin"],
                           capture_output=True, text=True)
        if r.returncode == 0:
            print(f"removed origin: {task['dir']}")


def main() -> None:
    done = sum(1 for t in TASKS if (WORK / t["dir"] / ".git").exists())
    print(f"work/ already has {done}/{len(TASKS)} repos")
    for t in TASKS:
        checkout(t)
    strip_remotes()
    print("setup complete")


if __name__ == "__main__":
    main()
