#!/usr/bin/env python3
"""Check that every command written in Tools/CodeGen/README.md still behaves as documented.

Rule (CLAUDE.md, "新功能必须同步对外文档"): a document that shows command lines and their output must be
checkable, otherwise it silently rots. This script reads README.md, runs every `CodeGen.exe ...` command
verbatim from the repository root, and checks two things:

1. the exit code equals the `[exit N]` line the document claims for it;
2. the **text** the document shows for that command really comes out of the tool.

(2) was added after the 2026-10-04 adversarial review measured a README whose serialization / hash product
snippets still said `#include "Serialization.h"` / the old `cacore` serializer / the old trait template while the
emitters had moved to `CodeGenSerialization.h` / `::cacore::codegen::writer` / `CodeGenEqual` — every example
still exited with the documented code, so checking exit codes alone was blind to it.

The text comparison is an **ordered subsequence** check, not an equality check: the README is allowed to elide
lines (it marks those with `...` / `…`), but every non-empty line that has no Chinese in it must appear in the
real output, in order. Lines containing Chinese are captions and prose, not tool output, so they are skipped.

Usage:
    python Tools/CodeGen/verify_readme.py            # run the checks
    python Tools/CodeGen/verify_readme.py --list     # just list what would run
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
README = Path(__file__).resolve().parent / "README.md"
EXE = REPO / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"

# A documented command line: starts with `CodeGen.exe ` and contains at least one argument that is not a
# placeholder (placeholders like `<file.h>` belong to the synopsis, not to a runnable example).
CMD_RE = re.compile(r"^CodeGen\.exe (.+)$")
EXIT_RE = re.compile(r"\[exit (\d+)\]")
# Lines carrying Chinese are the README's captions and truncation notes, not tool output.
HAS_CJK = re.compile(r"[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]")
# A field table's first line is `<TypeName> <fieldCount>`.
FIELD_HEAD_RE = re.compile(r"^\S+ \d+$")
# emitter flag -> the phrase that emitter's own header line uses, so a command that asks for one product is
# compared against THAT product's snippet and not against whichever snippet happens to come first.
EMITTER_KINDS = (("--emit-reflection", "CodeGen reflection emitter"),
                 ("--emit-serialization", "CodeGen serialization emitter"),
                 ("--emit-hash", "CodeGen hash"))


def prepare_output_dirs(argv: list) -> None:
    """Create the directories a documented command writes into.

    The examples write real files (`-o <path>`, `--emit-all --out-dir <dir>`). The tool deliberately does not
    create directories — it reports "cannot open output" (exit 2) instead, so a reader who forgot to create
    one sees that refusal rather than a silent success. That means the checker itself must create them,
    otherwise the examples only pass on a machine that happens to have the scratch directories left over.
    """
    out_dir = None
    for i, a in enumerate(argv):
        if a == "-o" and i + 1 < len(argv):
            Path(argv[i + 1]).parent.mkdir(parents=True, exist_ok=True)
        elif a == "--out-dir" and i + 1 < len(argv):
            out_dir = argv[i + 1]
        elif a.startswith("--out-dir="):
            out_dir = a[len("--out-dir="):]
    if out_dir:
        Path(out_dir).mkdir(parents=True, exist_ok=True)


def _fenced_blocks(lines: list, start: int, stop: int) -> list:
    """`(first, last)` line indexes of every ```-fenced block inside `[start, stop)`."""
    blocks, open_at = [], None
    for i in range(start, stop):
        if lines[i].strip().startswith("```"):
            if open_at is None:
                open_at = i
            else:
                blocks.append((open_at, i))
                open_at = None
    return blocks


def command_groups(lines: list) -> list:
    """Every documented command line as `(line index, position inside its fenced block)`.

    The README groups commands and then documents their outputs as the SAME number of blocks, in the same
    order (§三 documents four commands in one block and their four products much further down). Pairing the
    n-th command with the n-th following block is what keeps `--emit-hash` from being compared against the
    reflection snippet.
    """
    out, in_block, k = [], False, 0
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("```"):
            in_block = not in_block
            k = 0
            continue
        if in_block and CMD_RE.match(s) and "<" not in s:
            out.append((i, k))
            k += 1
    return out


def claimed_output(lines: list, cmd_index: int, argv: list, position: int):
    """The block that documents this command's output, or None.

    Shapes the README uses: a block whose first line is `[exit N]`, a product snippet naming the emitter, a
    field table starting with `<Type> <count>`, a fragment starting with `template<>`, and a bare refusal
    message (`CodeGen: ...` / `libclang: ...`). The search window starts after the fenced block the command
    lives in and ends at the next documented command, so a command never picks up the next example's output.
    """
    start = cmd_index + 1
    while start < len(lines) and not lines[start].strip().startswith("```"):
        start += 1
    start += 1
    stop = min(len(lines), start + 150)
    for j in range(start, len(lines)):
        if CMD_RE.match(lines[j].strip()):
            stop = j
            break
    candidates = []
    for first, last in _fenced_blocks(lines, start, stop):
        body = lines[first + 1:last]
        head = next((l.strip() for l in body if l.strip()), "")
        if (EXIT_RE.match(head) or head.startswith("// Generated by CodeGen")
                or head.startswith("CodeGen:") or head.startswith("libclang:")
                or FIELD_HEAD_RE.match(head) or head == "template<>"):
            candidates.append((head, body))
    if not candidates:
        return None
    if position < len(candidates):
        return candidates[position][1]
    # The block count did not line up with the command count — fall back to the emitter marker, then to the
    # first block, rather than silently checking nothing.
    for flag, marker in EMITTER_KINDS:
        if flag in argv:
            for _head, body in candidates:
                if any(marker in l for l in body):
                    return body
            break
    return candidates[0][1]


def _checkable(line: str) -> bool:
    """A claimed line worth comparing: non-empty, no Chinese, not an explicit truncation marker.

    Chinese-bearing lines are the README's captions ("（无 stdout / stderr；产物写在 -o 给的那个路径）") and
    truncation notes; comparing them would compare prose against tool output.
    """
    s = line.strip()
    if not s or HAS_CJK.search(s):
        return False
    if EXIT_RE.match(s):   # the exit marker is checked separately, it is not tool output
        return False
    return not (s.startswith("...") or s.startswith("…"))


def _norm(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def check_text(claimed: list, real: list):
    """Every checkable claimed line MUST appear in the real output, in order. `(compared, first_missing)`.

    Whitespace is collapsed first: the README wraps long tool messages across lines to stay readable and pads
    field-table columns with extra spaces, while the tool prints each message as ONE line with single spaces.
    Comparing raw lines flagged every wrapped example as a mismatch, which is a false alarm, not a stale doc.
    """
    hay = _norm("\n".join(real))
    pos, compared = 0, 0
    for line in claimed:
        if not _checkable(line):
            continue
        want = _norm(line)
        if not want:
            continue
        found = hay.find(want, pos)
        if found < 0:
            return compared, line.strip()
        pos = found + len(want)
        compared += 1
    return compared, None


def real_output(argv: list, proc) -> list:
    """What the command actually produced: the file it was told to write, else stdout **and stderr**.

    stderr counts because the README documents the refusal messages too (`refusing — 2 emitter flags in one
    run…`), and those are the lines a reader copies into a bug report.

    `--emit-all` writes three files and the README documents them as a name/size list (prose), so there is
    nothing to compare line-by-line — it returns an empty list and the text check is skipped for that example.
    """
    for i, a in enumerate(argv):
        if a == "-o" and i + 1 < len(argv):
            path = Path(argv[i + 1])
            if not path.exists():
                return []
            return path.read_text(encoding="utf-8", errors="replace").splitlines()
    if "--out-dir" in argv or any(a.startswith("--out-dir=") for a in argv):
        return []
    return (proc.stdout.decode("utf-8", "replace") + proc.stderr.decode("utf-8", "replace")).splitlines()


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if not README.exists():
        print(f"ERROR: no {README}")
        return 2
    if not EXE.exists():
        print(f"ERROR: {EXE} not built; run: python build.py --config Debug   "
              f"(CodeGen 是独立工程，脚本会用 out/build/codegen-tool 建它)")
        return 2

    lines = README.read_text(encoding="utf-8").splitlines()
    checked = bad = 0
    for i, position in command_groups(lines):
        line = lines[i]
        m = CMD_RE.match(line.strip())
        if not m:
            continue
        body = m.group(1)
        if "<" in body:  # synopsis, not a runnable example
            continue
        # A shell-style trailing comment is documentation, not an argument: a shell strips it, so this
        # checker must too (otherwise the tool receives `#` and the comment words as clang arguments and
        # fails to parse — which is exactly what the first version of this checker reported).
        comment = ""
        if " #" in body:
            body, comment = body.split(" #", 1)
            body = body.rstrip()
        argv = body.split()
        expect = None
        e = re.search(r"exit (\d+)", comment)
        if e:
            expect = int(e.group(1))
        else:
            for j in range(i + 1, min(i + 6, len(lines))):
                e2 = EXIT_RE.search(lines[j])
                if e2:
                    expect = int(e2.group(1))
                    break
        if args.list:
            print(f"  would run: CodeGen.exe {body}")
            continue
        prepare_output_dirs(argv)
        p = subprocess.run([str(EXE), *argv], cwd=str(REPO), capture_output=True)
        code_ok = (expect is None) or (p.returncode == expect)
        # The text check compares what this command really produced against the block that documents it.
        claimed = claimed_output(lines, i, argv, position)
        compared, missing = 0, None
        real = real_output(argv, p)
        if claimed and real:
            compared, missing = check_text(claimed, real)
        ok = code_ok and missing is None
        checked += 1
        bad += 0 if ok else 1
        print(f"{'OK  ' if ok else 'MISMATCH'}  exit={p.returncode} documented={expect} "
              f"text={compared}  CodeGen.exe {body}")
        if not code_ok:
            err = p.stderr.decode("utf-8", "replace").strip().splitlines()
            print("          stderr:", (err or ["(none)"])[0][:160])
        if missing is not None:
            print(f"          README shows a line the tool does not produce: {missing[:140]}")
    if args.list:
        return 0
    print(f"README: {checked} documented command(s) run, {bad} mismatch(es)")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
