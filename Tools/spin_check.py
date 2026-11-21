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
           A recorded path that the ARCHIVE MOVE relocated (`openspec/changes/<name>/…` ->
           `openspec/changes/archive/<date>-<name>/…`) is followed to its new home and counted as
           `relocated by archiving`, not as a failure — the command lines keep the text they were run
           with, because editing the record so that a checker turns green is editing the measurement.

Usage
-----
    python Tools/spin_check.py spin  start <round-id>
    python Tools/spin_check.py spin  end   <round-id>
    python Tools/spin_check.py queue start <round-id> [--change <名>]
    python Tools/spin_check.py queue end   <round-id> [--change <名>]
    python Tools/spin_check.py evidence [<evidence-dir>] [--change <名>]

Which change? (failure-visibility-hygiene 任务 4.1：不再是写死的已归档 change)
    `--change <名>` looks in `openspec/changes/<名>/` first, then in
    `openspec/changes/archive/*-<名>/` (exactly one match; more than one is an error naming them).
    Without the flag the tool runs `openspec list --json` and requires EXACTLY ONE active change;
    zero or several active changes exit non-zero and name them. `spin` does not read the change
    (it hashes the whole worktree), so the flag is accepted there but has no effect.
    An explicit `<evidence-dir>` positional argument still overrides the change's own evidence
    directory for `evidence`.

Snapshots land in `.review_tmp/spin/<round-id>.{spin,queue}.{start,end}.json` — a `.review_tmp/` path that
is gitignored, and an id chosen per round so a previous round's snapshot cannot be mistaken for this one.

`queue end` exit code: 0 when every item that was open at `start` is now either checked or was reported
by the agent as blocked/voided in the conversation; 1 when unchecked items remain that the tool cannot
see a reason for. The tool cannot read the agent's reasons, so it prints `still open: N` and the caller
(and the user) decides — but the NUMBER and the LIST are the program's, not the agent's.

`evidence` exit code: 0 when every recorded input path is tracked by git (at its recorded path, or at the
path the archive move gave it); 1 when at least one is not (typically a path under a gitignored directory,
or a machine-local absolute path).
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SNAP = REPO / ".review_tmp" / "spin"
ARCHIVE = REPO / "openspec" / "changes" / "archive"
CHANGES = REPO / "openspec" / "changes"


class ChangeLookupError(Exception):
    """A change directory could not be resolved (message is printed by `main`, exit code 2)."""


def _openspec_command() -> list[str]:
    """The `openspec list --json` command line, as a list a shell-free subprocess can actually start.

    On this machine `openspec` is an npm shim: the PATH entry named `openspec` is a Unix shell script,
    and the Windows-startable form is `openspec.cmd`. CreateProcess (what `subprocess` uses) neither
    appends PATHEXT nor runs `.cmd` files, so the shim must go through `cmd.exe /c` (measured: a bare
    `["openspec", ...]` fails with WinError 2).
    """
    exe = shutil.which("openspec")
    if exe is None:
        raise ChangeLookupError("找不到 `openspec` 命令（PATH 上没有可执行的 openspec / openspec.cmd）")
    if exe.lower().endswith((".cmd", ".bat")):
        return [os.environ.get("COMSPEC", "cmd.exe"), "/c", exe, "list", "--json"]
    return [exe, "list", "--json"]


def active_change_names() -> list[str]:
    """The active change names, as `openspec list --json` reports them (the tool's own list)."""
    command = _openspec_command()
    try:
        proc = subprocess.run(command, cwd=str(REPO),
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
    except OSError as error:
        raise ChangeLookupError(f"跑不了 `openspec list --json`：{error}") from error
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip().replace("\n", " ")[:400]
        raise ChangeLookupError(f"`openspec list --json` 失败（退出码 {proc.returncode}）：{detail}")
    try:
        data = json.loads(proc.stdout or "{}")
    except json.JSONDecodeError as error:
        raise ChangeLookupError(f"`openspec list --json` 的输出不是 JSON：{error}") from error
    entries = data.get("changes", []) if isinstance(data, dict) else data
    names: list[str] = []
    for entry in entries:
        if isinstance(entry, str):
            names.append(entry)
        elif isinstance(entry, dict):
            name = entry.get("name") or entry.get("change")
            if name:
                names.append(str(name))
    return names


def resolve_change_dir(explicit: str | None) -> Path:
    """The change directory named by `--change`, or the single active change when no name was given."""
    if explicit:
        direct = CHANGES / explicit
        if direct.is_dir():
            return direct
        matches = sorted(p for p in ARCHIVE.iterdir()
                         if p.is_dir() and p.name.endswith("-" + explicit))
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ChangeLookupError(
                f"change 名 {explicit} 在归档里匹配到多个：{', '.join(p.name for p in matches)}"
                f"；请改用完整目录名（--change <目录名>）")
        raise ChangeLookupError(f"找不到 change {explicit}：{CHANGES / explicit} 不存在，"
                                f"归档里也没有 *-{explicit}")
    names = active_change_names()
    if len(names) == 1:
        return CHANGES / names[0]
    if not names:
        raise ChangeLookupError("当前没有活跃 change；请用 --change <名> 显式指定（已归档 change 也可）")
    raise ChangeLookupError(f"当前有 {len(names)} 个活跃 change（{', '.join(names)}）；"
                            f"请用 --change <名> 显式指定")


def split_change_flag(argv: list[str]) -> tuple[list[str], str | None]:
    """Pull the optional `--change <名>` / `--change=<名>` flag out of the positional arguments."""
    rest: list[str] = []
    change: str | None = None
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--change":
            if index + 1 >= len(argv):
                raise ChangeLookupError("--change 需要 <change 名>")
            change = argv[index + 1]
            index += 2
            continue
        if arg.startswith("--change="):
            value = arg.split("=", 1)[1]
            if not value:
                raise ChangeLookupError("--change 需要 <change 名>")
            change = value
            index += 1
            continue
        rest.append(arg)
        index += 1
    return rest, change

# Deliverable file types for THIS change (engine-codegen-framework delivers code + build glue).
# `.txt` is included because the build glue is `CMakeLists.txt` — without it a round that ONLY edited a
# CMakeLists.txt was reported as `deliverables changed: 0`, i.e. a false spin round (found by the 9.27
# adversarial review; commit a23b633 really did touch three CMakeLists.txt files).
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


def task_items(tasks: Path) -> list[dict]:
    """Every checklist item in the given change's task list, in file order (id, checked, title)."""
    items: list[dict] = []
    if not tasks.exists():
        return items
    for line in tasks.read_text(encoding="utf-8").splitlines():
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


def queue(mode: str, round_id: str, tasks: Path) -> int:
    items = task_items(tasks)
    if not items:
        print(f"no checklist items found in {tasks}")
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


def _relocated_by_archive(rel: str) -> str | None:
    """The tracked path a recorded input moved to when its change was archived, else None.

    Archiving a finished change moves `openspec/changes/<name>/…` under
    `openspec/changes/archive/<date>-<name>/…`. The evidence files keep their command lines exactly as
    they were run (measured 2026-09-20: 39 distinct fixtures recorded under the pre-archive path), so the
    gate follows the move instead of the record being rewritten to make the gate green.
    """
    parts = rel.replace("\\", "/").split("/")
    if len(parts) < 4 or parts[0] != "openspec" or parts[1] != "changes" or parts[2] == "archive":
        return None
    tail = parts[3:]
    for d in sorted(p for p in ARCHIVE.iterdir() if p.is_dir() and p.name.endswith("-" + parts[2])):
        cand = d.joinpath(*tail)
        if cand.is_file():
            moved = cand.relative_to(REPO).as_posix()
            if _tracked(moved):
                return moved
    return None


def _input_paths(cmd_line: str) -> tuple[list[str], dict]:
    """Input paths a recorded command consumes, plus a tally of what was skipped and why.

    Skipped on purpose (each reason is COUNTED and printed, so the summary cannot read as full coverage
    when it is not — a round-33 adversarial review showed the earlier version printed only the number of
    checked paths, while silently dropping whole classes of them):
      * the first token — that is the program being run, not a file it reads;
      * the token after `-o` — that is the OUTPUT path, machine-local by design;
      * `*.exe` — build products, which the build never commits;
      * absolute paths — machine-local by definition. Absolute Windows paths are masked out BEFORE the
        whitespace split, because `C:\\Program Files (x86)\\...` would otherwise be shredded into
        fragments that look like repository-relative paths;
      * anything under `out/` — build tree;
      * a token with NO path separator — a bare file name is ambiguous (`python verify_round1.py` names
        eight different files in this repository).

    Counted, not skipped (each of these could hide an out-of-repo input):
      * an include directory written ATTACHED to its flag, e.g. `-I.review_tmp/probe`;
      * a quoted path;
      * a path written with a `./` prefix;
      * a path that escapes the repository (`../probe/x.h`);
      * a path whose first segment is not an entry of the repository at all, e.g. `probe/secret.h` — that
        cannot be a tracked file, so it is reported as not-tracked rather than dropped.
    """
    tally = {"after -o": 0, "absolute (outside the repo)": 0, "exe": 0, "out/": 0, "flag": 0,
             "no separator": 0}
    attach_flags = ("-I", "-isystem", "-iquote", "-idirafter", "-include")
    toks = cmd_line.split()
    if not toks:
        return [], tally
    out: list[str] = []
    skip_next = False
    seen_positional = False
    i = 1  # toks[0] is the program being run, never an input it reads
    while i < len(toks):
        raw = toks[i]
        i += 1
        if raw == "-o":
            skip_next = True
            continue
        if skip_next:
            skip_next = False
            tally["after -o"] += 1
            continue
        t = raw.strip('"')
        # An absolute Windows path may contain spaces, so the whitespace split shreds it. Re-join the
        # following tokens until the accumulated path is an existing FILE, ends in a known file
        # extension, or the next token is a flag. (A path that really is one token is left alone, so the
        # positional arguments that follow it stay separate.)
        if re.match(r"^[A-Za-z]:[\\/]", t):
            while (i < len(toks) and not toks[i].startswith("-")
                   and not Path(t).is_file()
                   and not re.search(r"\.(h|hpp|hh|cpp|cxx|cc|py|md|txt|json|cmake|exe|dll)$", t, re.I)):
                t = t + " " + toks[i]
                i += 1
        if raw.startswith("-"):
            t = raw.strip('"')
            for flag in attach_flags:
                if t.startswith(flag) and len(t) > len(flag):
                    t = t[len(flag):].lstrip("=")  # `-I<dir>` / `-isystem=<dir>`
                    break
            else:
                tally["flag"] += 1
                continue
        # The FIRST positional argument of a recorded command is what the program was told to read, so
        # it is an input whatever its spelling (a bare `myfixture.h` names no repository file).
        first_positional = not seen_positional
        if first_positional:
            seen_positional = True
        if t.lower().endswith(".exe") and not first_positional:
            tally["exe"] += 1
            continue
        m_abs = re.match(r"^([A-Za-z]):[\\/](.*)$", t)
        if m_abs:
            # An absolute path that points INSIDE the repository is a repository-relative path written
            # the long way; one that points outside (SDK headers, the compiler) is machine-local.
            drive_rel = (m_abs.group(1) + ":/" + m_abs.group(2).replace("\\", "/"))
            root = REPO.as_posix()
            if drive_rel.lower().startswith(root.lower() + "/"):
                out.append(drive_rel[len(root) + 1:])
            else:
                tally["absolute (outside the repo)"] += 1
            continue
        if t.startswith("/") or t.startswith("\\\\"):
            tally["absolute (outside the repo)"] += 1
            continue
        if re.match(r"^out[\\/]", t) or "/out/" in t or "\\out\\" in t:
            if not first_positional:
                tally["out/"] += 1
                continue
        if not re.search(r"[\\/]", t):
            if first_positional:
                out.append(t)
            else:
                tally["no separator"] += 1
            continue
        norm = t[2:] if t.startswith("./") else t
        if norm.startswith(".."):
            out.append(t)  # escapes the repository -> can never be tracked
            continue
        out.append(t)
    return out, tally


def evidence(evidence_dir: Path) -> int:
    files = sorted(evidence_dir.glob("*.txt"))
    if not files:
        print(f"no evidence .txt files under {evidence_dir}")
        return 2
    checked = failed = total_paths = skipped_total = relocated_total = 0
    for f in files:
        lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
        cmds = [l.split(":", 1)[1].strip() for l in lines if l.lower().startswith("cmd:")]
        if not cmds:
            print(f"  {f.name}: no `cmd:` line (not covered by this gate)")
            continue
        checked += 1
        bad: list[str] = []
        moved: dict[str, str] = {}
        extra = 0
        n = 0
        tally: dict = {}
        for c in cmds:
            paths, t = _input_paths(c)
            for k, v in t.items():
                tally[k] = tally.get(k, 0) + v
                skipped_total += v
            for p in paths:
                n += 1
                total_paths += 1
                if _tracked(p):
                    continue
                twin = _relocated_by_archive(p)
                if twin:
                    relocated_total += 1
                    if p in moved:
                        extra += 1
                    else:
                        moved[p] = twin
                else:
                    bad.append(p)
        state = "OK" if not bad else f"NOT-IN-REPO {len(bad)}"
        skipped = ", ".join(f"{v} {k}" for k, v in sorted(tally.items()) if v)
        notes = ([f"relocated by archiving: {sum(1 for _ in moved) + extra}"] if moved else []) \
            + ([f"skipped: {skipped}"] if skipped else [])
        print(f"  {f.name}: {len(cmds)} cmd line(s), {n} input path(s) -> {state}"
              f"{'   [' + '; '.join(notes) + ']' if notes else ''}")
        for old, new in list(moved.items())[:3]:
            print(f"      relocated by archiving: {old} -> {new}")
        if len(moved) > 3:
            print(f"      ... and {len(moved) - 3} more relocated path(s)")
        for p in bad:
            print(f"      not tracked by git: {p}")
        failed += len(bad)
    print(f"evidence: {checked} file(s) with `cmd:` lines, {total_paths} input path(s), "
          f"{failed} not tracked by git, {relocated_total} relocated by archiving")
    if relocated_total:
        print("  note: `relocated by archiving` paths were checked at their post-archive location; the "
              "command lines keep the pre-archive text they were run with")
    if skipped_total:
        print("  note: tokens skipped above are NOT inputs (flags, the program, -o targets, bare file "
              "names); they are printed so the count is not read as full coverage")
    return 1 if failed else 0


def main() -> int:
    # Task titles are Chinese; a cp1252 console would crash on them (measured), so force UTF-8 output.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    try:
        argv, explicit_change = split_change_flag(sys.argv[1:])
    except ChangeLookupError as error:
        print(str(error))
        return 2
    if argv and argv[0] == "evidence":
        if len(argv) > 2:
            print("evidence 只接受一个可选的 <evidence-dir> 位置参数")
            return 2
        if len(argv) == 2:
            # 显式目录优先：给定了就不再要求解析 change（没有活跃 change 时也照样能查归档记录）。
            return evidence(Path(argv[1]))
        try:
            change_dir = resolve_change_dir(explicit_change)
        except ChangeLookupError as error:
            print(str(error))
            return 2
        return evidence(change_dir / "evidence")
    if len(argv) != 3 or argv[0] not in ("spin", "queue") or argv[1] not in ("start", "end"):
        print(__doc__)
        return 2
    SNAP.mkdir(parents=True, exist_ok=True)
    what, mode, round_id = argv
    if what == "spin":
        return spin(mode, round_id)
    try:
        change_dir = resolve_change_dir(explicit_change)
    except ChangeLookupError as error:
        print(str(error))
        return 2
    return queue(mode, round_id, change_dir / "tasks.md")


if __name__ == "__main__":
    raise SystemExit(main())
