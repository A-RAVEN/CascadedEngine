#!/usr/bin/env python3
"""Check that every command written in Tools/CodeGen/README.md still behaves as documented.

Rule (CLAUDE.md, "新功能必须同步对外文档"): a document that shows command lines and their output must be
checkable, otherwise it silently rots. This script reads README.md, runs every `CodeGen.exe ...` command
verbatim from the repository root, and compares the exit code with the `[exit N]` line the document claims
for it. It does not compare stdout text (that would duplicate the document); it compares the part a reader
acts on — does this command do what the document says?

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
        print(f"ERROR: {EXE} not built; run: python build.py --config Debug CodeGen")
        return 2

    lines = README.read_text(encoding="utf-8").splitlines()
    checked = bad = 0
    for i, line in enumerate(lines):
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
        p = subprocess.run([str(EXE), *argv], cwd=str(REPO), capture_output=True)
        ok = (expect is None) or (p.returncode == expect)
        checked += 1
        bad += 0 if ok else 1
        print(f"{'OK  ' if ok else 'MISMATCH'}  exit={p.returncode} documented={expect}  CodeGen.exe {body}")
        if not ok:
            err = p.stderr.decode("utf-8", "replace").strip().splitlines()
            print("          stderr:", (err or ["(none)"])[0][:160])
    if args.list:
        return 0
    print(f"README: {checked} documented command(s) run, {bad} mismatch(es)")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
