#!/usr/bin/env python3
"""Re-run the six archived evidence scripts and write the result as one generated file (task 9.52(2)).

Why this exists. The previous `r29-scripts-rerun.txt` was assembled by hand across three moments and
contained three sections that contradicted each other:

  * the first section recorded `FileNotFoundError` for the four `verify_round*.py` scripts;
  * the second recorded `UnicodeEncodeError` on cp1252;
  * the third recorded "runs, but exit 1 with `?` marks everywhere".

Nothing said which section was current, the Chinese check names had been destroyed (67 `?`), and the
file's own summary said 7 failures while the body named 6. This runner replaces all three with ONE
generated pass:

  * every script is executed as a subprocess, its stdout/stderr captured as BYTES and decoded as UTF-8
    (each script reconfigures its own streams to UTF-8, so the Chinese names survive);
  * the summary at the bottom is COUNTED from the captured text, so the total cannot disagree with the
    listed failures;
  * the working directory is a parameter, so "the scripts work from any directory" is a measurement
    instead of a claim.

Usage
-----
    python rerun_scripts.py --out <evidence file>            # from the repo root
    python rerun_scripts.py --out <evidence file> --cwd openspec/changes/engine-codegen-framework
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
SCRIPTS = [
    "verify_round1.py",
    "verify_round2.py",
    "verify_round3.py",
    "verify_round18.py",
    "check_codegen_failure.py",
    "check_emit_order.py",
]


def run(script: str, cwd: Path) -> dict:
    path = HERE / script
    argv = [sys.executable, str(path)]
    shown = ["python", path.relative_to(REPO).as_posix()]
    try:
        proc = subprocess.run(argv, cwd=str(cwd), capture_output=True, timeout=900)
        code, out, err = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as exc:
        code, out, err = "timeout", exc.stdout or b"", exc.stderr or b""
    text = (out + err).decode("utf-8", errors="replace")
    failed = [l.strip() for l in text.splitlines() if "FAILED" in l and not l.strip().startswith("TOTAL")]
    return {"script": script, "shown": shown, "exit": code, "text": text, "failed": failed}


def render(cwd_label: str, results: list[dict]) -> str:
    lines = [f"## run with the working directory set to {cwd_label}", ""]
    lines.append("# the cmd lines below are written RELATIVE TO THE REPOSITORY ROOT, so that the same line")
    lines.append("# reads the same in every section. The process actually executed an ABSOLUTE script path with")
    lines.append("# the working directory named in this heading; copying a cmd line into a shell that sits in")
    lines.append("# that working directory would fail, because the path would be resolved twice.")
    lines.append("")
    for r in results:
        lines.append(f"===== {r['script']} =====")
        lines.append("cmd: " + " ".join(r["shown"]))
        lines.append(f"exit: {r['exit']}")
        lines.append(f"lines containing FAILED: {len(r['failed'])}")
        lines.append("--- output ---")
        for line in r["text"].splitlines():
            lines.append(line)
        if not r["text"].strip():
            lines.append("(no output)")
        lines.append("")
    nonzero = [r["script"] for r in results if r["exit"] != 0]
    all_failed = [(r["script"], f) for r in results for f in r["failed"]]
    lines.append("## SUMMARY")
    lines.append(f"scripts run: {len(results)}")
    lines.append(f"scripts with a non-zero exit: {len(nonzero)} -> {', '.join(nonzero) or '(none)'}")
    lines.append(f"lines containing FAILED, counted from the captured output: {len(all_failed)}")
    for script, f in all_failed:
        lines.append(f"  {script}: {f}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--cwd", default=".")
    ap.add_argument("--label", default="")
    ap.add_argument("--append", action="store_true")
    args = ap.parse_args()

    cwd = (REPO / args.cwd).resolve()
    if not cwd.is_dir():
        print(f"ERROR: no such directory: {cwd}")
        return 2
    label = args.label or cwd.relative_to(REPO).as_posix() or "."

    results = [run(s, cwd) for s in SCRIPTS]
    text = render(label, results)

    out_path = Path(args.out)
    if not out_path.is_absolute():
        out_path = REPO / out_path
    with out_path.open("a" if args.append else "w", encoding="utf-8", newline="\n") as f:
        if args.append:
            f.write("\n")
        f.write(text)

    for r in results:
        print(f"  exit={str(r['exit']):>8}  failed-lines={len(r['failed']):>2}  {r['script']}")
    print(f"cwd={label} scripts={len(results)} -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
