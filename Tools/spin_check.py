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

Usage
-----
    python Tools/spin_check.py spin  start <round-id>
    python Tools/spin_check.py spin  end   <round-id>
    python Tools/spin_check.py queue start <round-id>
    python Tools/spin_check.py queue end   <round-id>

Snapshots land in `.review_tmp/spin/<round-id>.{spin,queue}.{start,end}.json` — a `.review_tmp/` path that
is gitignored, and an id chosen per round so a previous round's snapshot cannot be mistaken for this one.

`queue end` exit code: 0 when every item that was open at `start` is now either checked or was reported
by the agent as blocked/voided in the conversation; 1 when unchecked items remain that the tool cannot
see a reason for. The tool cannot read the agent's reasons, so it prints `still open: N` and the caller
(and the user) decides — but the NUMBER and the LIST are the program's, not the agent's.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SNAP = REPO / ".review_tmp" / "spin"
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


def main() -> int:
    # Task titles are Chinese; a cp1252 console would crash on them (measured), so force UTF-8 output.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    if len(sys.argv) != 4 or sys.argv[1] not in ("spin", "queue") or sys.argv[2] not in ("start", "end"):
        print(__doc__)
        return 2
    SNAP.mkdir(parents=True, exist_ok=True)
    what, mode, round_id = sys.argv[1], sys.argv[2], sys.argv[3]
    return spin(mode, round_id) if what == "spin" else queue(mode, round_id)


if __name__ == "__main__":
    raise SystemExit(main())
