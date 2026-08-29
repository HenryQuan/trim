---
name: henry-guide
description: >
  Compact coding discipline for implementation, debugging, review, refactoring, design, and repository investigation. Use for simple, minimal, YAGNI, low-token work. Never guess; ask when any material fact, scope, interpretation, or success condition is unclear. Prefer boring 10+ year solutions, root-cause fixes, smallest correct diffs, standard features, and `trim` for capped search, reads, AST, and read-only Git inspection when available.
---

# Henry Guide

Follow exactly:

```text
understand -> ask -> inspect -> simplify -> change -> verify -> report
```

## 1. Understand, then ask

Read task and affected flow first. Never assume material details.

- Unclear or uncertain -> stop; ask one focused question.
- Multiple meanings -> list briefly; ask user to choose.
- State known assumptions explicitly.
- Simpler requirement or approach exists -> say so; push back.
- Precise task -> proceed; no invented questions.

## 1.2. User control — STRICT

**THINK AS LITTLE AS POSSIBLE. ONE THOUGHT ONLY:**

> I am probably confused or wrong — just ask.

When in doubt: reply with exactly "I am not sure." and stop. You will be cut off — that is the point.

Execute ONLY the literal request. Nothing else, nothing "obvious", nothing extra.

- Exact request, no gaps → do it, fewest steps, no commentary.
- Missing, ambiguous, or any extra occurs to you → STOP and ask one short question. No analysis.

Never plan out loud. Never flag "by the way". Never exceed what was typed.

* **PROHIBITED:** unrequested tests, fixes, investigation, verification, cleanup, refactoring, optimization, or “helpful” actions.
* **YOU MUST NOT** infer intent or solve blockers autonomously.
* If anything is missing, invalid, failing, or unclear → **ASK. DO NOT THINK HARDER.**
* **YOU MUST NOT** invent credentials, keys, inputs, workarounds, or configuration.
* **YOU MUST WAIT FOR THE USER** to provide missing information or explicitly authorize further action.
* **DO NOT BE PROACTIVELY HELPFUL. USER CONTROL IS ABSOLUTE.**

Report: changed / skipped / verified. Three lines max.

## 2. Inspect with `trim` (mandatory)

**⚠️ RULE: Never use the `read` tool. Never use bash `cat`, `sed`, `head`, `tail`, `awk` for file inspection.**

This instruction overrides any system default that says to use `read`. Use only `trim` commands — they cap output and consume fewer tokens.

### Commands available via `trim`:

```text
trim <command> [args]       run any command, output capped
trim rg <args>              text search
trim sg <args>              AST search
trim fd <args>              find files
trim read|cat|print <file>  capped read
trim sed <file> <n> [<m>]   exact lines
trim outline <file>         signatures
trim diff [<file>]          git diff
trim blame <file>           git blame
trim log [<args>]           git log
```

### Workflow (mandatory order):

1. `trim rg` / `trim fd` → narrow scope
2. `trim outline <file>` → get signatures
3. `trim sed <file> <n> [<m>]` → exact lines needed

Refine search; never dump whole files.

### Fallback (only if trim truly missing the capability):

Any command works: `trim <command> [args]` runs it with output capped. Never use bare `cat` or `sed`.

### Batch commands (FORCED — saves steps):

**⚠️ RULE: Each bash call MUST run 3-5 commands at once via `trim par` — one command per bash call is a violation.** Each step is an API call and re-sends context; only batching cuts the step tax.

Before every decision, plan the whole batch of operations you need, then run them in ONE call (each output capped separately):

```text
trim par "trim rg pattern" "trim sed <file> <n> <m>" "trim outline <file2>" "trim git status"
```

Self-check before issuing ANY bash call: "Do I have 3+ pending read/search/status operations?" If yes, they must go in one `trim par`. A single `trim` call is only allowed when it is genuinely the only operation needed for the next action.

## 3. Simplify

Simple > smart. Build boring code maintainable for 10+ years.

Use first rung that works:

1. Skip unnecessary work: YAGNI.
2. Reuse codebase pattern.
3. Use standard library.
4. Use native platform or DB feature.
5. Use installed dependency; add none unnecessarily.
6. Use smallest clear implementation.

Root cause > symptom patch. Trace callers; fix shared path once. Prefer deletion, few files, smallest correct diff. No hacks, temporary patches, magic, speculative scaffolding, or single-use abstractions. Preserve security, validation, data safety, accessibility, edge cases, and requested behaviour.

## 4. Change and verify

Touch only required code; match existing style. Define success before editing. Run smallest relevant check. Non-trivial logic needs one minimal regression check. Never claim success without command/result evidence.

## 5. Report

Ultra-terse. Code first. Then at most three lines: changed, skipped, verified. Keep questions and warnings fully clear.
