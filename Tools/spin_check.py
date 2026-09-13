"""Round-boundary spin check (tasks 9.24 / 9.28).

Compares CONTENT HASHES of this change's deliverable files between the start and the end of a round and
prints exactly which deliverables moved.

Why not `git diff -w --numstat`: that form compares the worktree against the INDEX (a staged change is
invisible), has no pathspec, and counts document edits as work. This tool instead hashes a FIXED
deliverable list — source files only — with no dependence on git state at all.

Usage
-----
    python Tools/spin_check.py start <round-id>
    python Tools/spin_check.py end   <round-id>

Snapshots land in `.review_tmp/spin/<round-id>.{start,end}.json` (an id that has not been used this
session, so a previous round's snapshot can never be mistaken for this one).

Exit code of `end`: 0 always (this is a report, not a gate) — the caller decides. The printed line
`deliverables changed: N` is the machine-readable verdict; `N == 0` means the round touched no
deliverable (a documentation-only round, i.e. a spin round for a code deliverable).
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SNAP = REPO / ".review_tmp" / "spin"

# Deliverable file types for THIS change (engine-codegen-framework delivers code + build glue).
SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cxx", ".cc", ".py", ".cmake", ".def"}
EXCLUDE_DIRS = {"out", ".git", ".review_tmp", ".claude", "node_modules", ".vs"}


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


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in ("start", "end"):
        print(__doc__)
        return 2
    mode, round_id = sys.argv[1], sys.argv[2]
    SNAP.mkdir(parents=True, exist_ok=True)
    current = snapshot()
    if mode == "start":
        (SNAP / f"{round_id}.start.json").write_text(json.dumps(current, indent=1), encoding="utf-8")
        print(f"round {round_id}: snapshot taken over {len(current)} deliverable files -> "
              f"{SNAP / (round_id + '.start.json')}")
        return 0

    start_file = SNAP / f"{round_id}.start.json"
    if not start_file.exists():
        print(f"no start snapshot for round {round_id}; run `start` first")
        return 2
    before = json.loads(start_file.read_text(encoding="utf-8"))
    (SNAP / f"{round_id}.end.json").write_text(json.dumps(current, indent=1), encoding="utf-8")

    added = sorted(set(current) - set(before))
    removed = sorted(set(before) - set(current))
    changed = sorted(p for p in set(before) & set(current) if before[p] != current[p])
    print(f"deliverables changed: {len(added) + len(removed) + len(changed)}")
    for label, items in (("modified", changed), ("added", added), ("removed", removed)):
        for p in items:
            print(f"  {label}: {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
