"""Round-boundary checks for a goal round (tasks 9.24 / 9.28 / the 2026-09-13 goal revision).

Two subcommands, both designed so the FACTS come from a program instead of from memory:

  spin   — compare CONTENT HASHES of the change's deliverable files between round start and end, and
           print exactly which deliverables moved. (Why not `git diff -w --numstat`: it compares the
           worktree against the INDEX, so a staged edit is invisible, it has no pathspec, and it counts
           document edits as work.)

  queue  — read the OpenSpec change's task list and print the items that are still UNCHECKED. `start`
           freezes that list for the round; `end` diffs against it and prints what got closed and what
           is still open. This exists because the "did the round do everything it could?" check used to
           depend on the agent quoting the list from memory — and memory is exactly what shrinks.

  evidence — read the CHANGE's evidence files and check that every INPUT path a recorded command
           consumes is a TRACKED repository file. This exists because a previous evidence file recorded
           its commands against `.review_tmp/probe/*.h`, a gitignored directory: the measurements were
           real, but "a clean checkout reproduces this output" was false, and nothing said so.

Usage
-----
    python Tools/spin_check.py spin  start <round-id>
    python Tools/spin_check.py spin  end   <round-id>
    python Tools/spin_check.py queue start <round-id>
    python Tools/spin_check.py queue end   <round-id>
    python Tools/spin_check.py evidence [<evidence-dir>]

Snapshots land in `.review_tmp/spin/<round-id>.{spin,queue}.{start,end}.json` — a `.review_tmp/` path that
is gitignored, and an id chosen per round so a previous round's snapshot cannot be mistaken for this one.

`queue end` exit code: 0 when every item that was open at `start` is now either checked or was reported
by the agent as blocked/voided in the conversation; 1 when unchecked items remain that the tool cannot
see a reason for. The tool cannot read the agent's reasons, so it prints `still open: N` and the caller
(and the user) decides — but the NUMBER and the LIST are the program's, not the agent's.

`evidence` exit code: 0 when every recorded input path is tracked by git; 1 when at least one is not
(typically a path under a gitignored directory, or a machine-local absolute path).
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SNAP = REPO / ".review_tmp" / "spin"
EVIDENCE = REPO / "openspec" / "changes" / "engine-codegen-framework" / "evidence"
TASKS = REPO / "openspec" / "changes" / "engine-codegen-framework" / "tasks.md"

# Deliverable file types for THIS change (engine-codegen-framework delivers code + build glue).
# `.txt` is included because the build glue is `CMakeLists.txt` — without it a round that ONLY edited a
# CMakeLists.txt was reported as `deliverables changed: 0`, i.e. a false spin round (found by the 9.27
# adversarial review; commit 1b9c6cb really did touch three CMakeLists.txt files).
SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cxx", ".cc", ".py", ".cmake", ".txt", ".def"}
EXCLUDE_DIRS = {"out", ".git", ".review_tmp", ".claude", "node_modules", ".vs"}

# `- [ ] 9.29 ...` / `- [x] 9.29 ...` at the start of a line.
ITEM_RE = re.compile(r"^- \[([ xX])\]\s+(\S+)")


def snapshot() -> dict[str, str]:
    out: dict[str, str] = {}
    for path in REPO.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SUFFIXES:
            continue
        rel = path.relative_to(REPO)
        if any(part in EXCLUDE_DIRS for part in rel.parts):
            continue
        out[rel.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return out


def task_items() -> list[dict]:
    """Every checklist item in the change's task list, in file order (id, checked, title)."""
    items: list[dict] = []
    if not TASKS.exists():
        return items
    for line in TASKS.read_text(encoding="utf-8").splitlines():
        m = ITEM_RE.match(line)
        if not m:
            continue
        title = line.split("]", 1)[1].strip()
        items.append({
            "id": m.group(2),
            "checked": m.group(1).lower() == "x",
            "title": title[:100],
        })
    return items


def spin(mode: str, round_id: str) -> int:
    current = snapshot()
    if mode == "start":
        (SNAP / f"{round_id}.spin.start.json").write_text(json.dumps(current, indent=1), encoding="utf-8")
        print(f"spin {round_id}: snapshot taken over {len(current)} deliverable files -> "
              f"{SNAP / (round_id + '.spin.start.json')}")
        return 0

    start_file = SNAP / f"{round_id}.spin.start.json"
    if not start_file.exists():
        print(f"no spin start snapshot for round {round_id}; run `spin start` first")
        return 2
    before = json.loads(start_file.read_text(encoding="utf-8"))
    (SNAP / f"{round_id}.spin.end.json").write_text(json.dumps(current, indent=1), encoding="utf-8")

    added = sorted(set(current) - set(before))
    removed = sorted(set(before) - set(current))
    changed = sorted(p for p in set(before) & set(current) if before[p] != current[p])
    print(f"deliverables changed: {len(added) + len(removed) + len(changed)}")
    for label, items in (("modified", changed), ("added", added), ("removed", removed)):
        for p in items:
            print(f"  {label}: {p}")
    return 0


def queue(mode: str, round_id: str) -> int:
    items = task_items()
    if not items:
        print(f"no checklist items found in {TASKS}")
        return 2
    state = {i["id"]: i["checked"] for i in items}
    titles = {i["id"]: i["title"] for i in items}
    path = SNAP / f"{round_id}.queue.{mode}.json"

    if mode == "start":
        path.write_text(json.dumps(state, indent=1), encoding="utf-8")
        open_ids = [i["id"] for i in items if not i["checked"]]
        print(f"queue {round_id}: {len(items)} items total, {len(open_ids)} OPEN at round start "
              f"(frozen -> {path})")
        for i in open_ids:
            print(f"  OPEN  {i}  {titles[i]}")
        return 0

    start_file = SNAP / f"{round_id}.queue.start.json"
    if not start_file.exists():
        print(f"no queue start snapshot for round {round_id}; run `queue start` first")
        return 2
    before = json.loads(start_file.read_text(encoding="utf-8"))
    path.write_text(json.dumps(state, indent=1), encoding="utf-8")

    was_open = [k for k, v in before.items() if not v]
    closed = [k for k in was_open if state.get(k)]
    still_open = [k for k in was_open if not state.get(k)]
    newly_open = [k for k, v in state.items() if not v and k not in before]

    print(f"queue {round_id}: closed this round: {len(closed)}")
    for i in closed:
        print(f"  CLOSED {i}")
    print(f"still open (was open at start, still unchecked): {len(still_open)}")
    for i in still_open:
        print(f"  STILL-OPEN {i}  {titles.get(i, '')}")
    if newly_open:
        print(f"opened by this round: {len(newly_open)}")
        for i in newly_open:
            print(f"  NEW-OPEN {i}  {titles.get(i, '')}")
    print(f"OPEN-COUNT {len([k for k, v in state.items() if not v])}")
    # Honest gate (found by the 9.27 adversarial review): this USED to return 0 unconditionally while
    # the docstring promised exit 1 — a gate that can never fire. It now returns 1 when items that were
    # open at `start` are still unchecked. NOTE what it still cannot do: it only reads the CHECKBOX, so
    # "closed" means someone typed `- [x]`; it is a bookkeeping gate, not a proof of work.
    return 1 if still_open else 0


def _tracked(rel: str) -> bool:
    r = subprocess.run(["git", "ls-files", "--error-unmatch", "--", rel],
                       cwd=str(REPO), capture_output=True, text=True)
    return r.returncode == 0


def _input_paths(cmd_line: str) -> list[str]:
    """Input paths a recorded command consumes.

    Skipped on purpose, and why (the gate would otherwise be noise instead of a gate):
      * the first token — that is the program being run, not a file it reads;
      * the token after `-o` — that is the OUTPUT path, machine-local by design;
      * `*.exe` — build products, which the build never commits;
      * absolute paths (a drive letter or a leading slash) — machine-local by definition, and the
        whitespace split below shreds `C:\\Program Files (x86)\\...` into fragments anyway;
      * anything under `out/` — build tree;
      * a token with NO path separator — a bare file name is ambiguous (and would make `python foo.py`
        fail even when an identically named tracked file exists elsewhere);
      * a token whose FIRST path segment is not an entry in the repository — that is a leftover
        fragment of a space-containing absolute path (`Files\\Microsoft`, `Kits\\10\\...`).

    Counted, not skipped (the round-32 adversarial review showed each of these could hide an out-of-repo
    input from an earlier version of this gate):
      * an include directory written ATTACHED to its flag, e.g. `-I.review_tmp/probe`;
      * a quoted path (`".review_tmp/probe/my fixture.h"` — the split may leave a fragment, which is
        reported as not tracked, which is the truthful outcome);
      * a path written with a `./` prefix;
      * a path that escapes the repository (`../probe/x.h`) — that can never be a tracked repo file, so
        it is reported as not tracked instead of being silently dropped.
    """
    toks = cmd_line.split()
    if not toks:
        return []
    top = {p.name for p in REPO.iterdir()}
    attach_flags = ("-I", "-isystem", "-iquote", "-idirafter", "-include")
    out: list[str] = []
    skip_next = False
    for i, raw in enumerate(toks):
        if i == 0:
            continue
        if skip_next:
            skip_next = False
            continue
        if raw == "-o":
            skip_next = True
            continue
        t = raw.strip('"')
        if raw.startswith("-"):
            for flag in attach_flags:
                if t.startswith(flag) and len(t) > len(flag):
                    t = t[len(flag):]        # `-I<dir>` with the value attached
                    break
            else:
                continue                      # a plain flag, not a path
        if t.lower().endswith(".exe"):
            continue
        if re.match(r"^[A-Za-z]:[\\/]", t) or t.startswith("/") or t.startswith("\\\\"):
            continue
        if re.match(r"^out[\\/]", t) or "/out/" in t or "\\out\\" in t:
            continue
        if not re.search(r"[\\/]", t):
            continue
        norm = t[2:] if t.startswith("./") else t
        if norm.startswith(".."):
            out.append(t)                     # escapes the repo -> can never be tracked
            continue
        first = re.split(r"[\\/]", norm)[0]
        if first not in top:
            continue
        out.append(t)
    return out


def evidence(evidence_dir: Path) -> int:
    files = sorted(evidence_dir.glob("*.txt"))
    if not files:
        print(f"no evidence .txt files under {evidence_dir}")
        return 2
    checked = failed = total_paths = 0
    for f in files:
        lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
        cmds = [l.split(":", 1)[1].strip() for l in lines if l.lower().startswith("cmd:")]
        if not cmds:
            print(f"  {f.name}: no `cmd:` line (not covered by this gate)")
            continue
        checked += 1
        bad: list[str] = []
        n = 0
        for c in cmds:
            for p in _input_paths(c):
                n += 1
                total_paths += 1
                if not _tracked(p):
                    bad.append(p)
        state = "OK" if not bad else f"NOT-IN-REPO {len(bad)}"
        print(f"  {f.name}: {len(cmds)} cmd line(s), {n} input path(s) -> {state}")
        for p in bad:
            print(f"      not tracked by git: {p}")
        failed += len(bad)
    print(f"evidence: {checked} file(s) with `cmd:` lines, {total_paths} input path(s), "
          f"{failed} not tracked by git")
    return 1 if failed else 0


def main() -> int:
    # Task titles are Chinese; a cp1252 console would crash on them (measured), so force UTF-8 output.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    if len(sys.argv) >= 2 and sys.argv[1] == "evidence":
        target = Path(sys.argv[2]) if len(sys.argv) > 2 else EVIDENCE
        return evidence(target)
    if len(sys.argv) != 4 or sys.argv[1] not in ("spin", "queue") or sys.argv[2] not in ("start", "end"):
        print(__doc__)
        return 2
    SNAP.mkdir(parents=True, exist_ok=True)
    what, mode, round_id = sys.argv[1], sys.argv[2], sys.argv[3]
    return spin(mode, round_id) if what == "spin" else queue(mode, round_id)


if __name__ == "__main__":
    raise SystemExit(main())
