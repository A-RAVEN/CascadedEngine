#!/usr/bin/env python3
"""Regenerate the CodeGen gate/content evidence files from a REAL binary.

Why this exists (tasks 9.51 / 9.52): the previous evidence files were hand-transcribed.
`r28-gates.txt` admitted in its own first line that every record was an AFTER state, so
the "before" (red) state existed only as a parenthetical sentence in `tasks.md`; and its
commands pointed at `.review_tmp/probe/*.h`, a gitignored directory, so "a clean checkout
reproduces this output" was false. `r31-950.txt` recorded no commands at all.

This script makes both files GENERATED rather than transcribed:

  * every case records the exact argv, the exit code, whether `-o <path>` left an
    artifact, and the full stdout/stderr (written as UTF-8 bytes, never through a
    console, so Chinese diagnostic text survives);
  * the only inputs are tracked repository files (`evidence/fixtures/*.h`,
    `Tools/CodeGen/*.h`), so `Tools/spin_check.py evidence` can gate on it;
  * the section header records the binary's identity (path + SHA256) plus the source
    blob it was built from, so a reader can tell which code state produced it.

Rebuild recipe for a historical binary (used for the BEFORE sections):

    git checkout <commit> -- Tools/CodeGen/CodeGenMain.cpp
    python build.py --config Debug --no-configure CodeGen
    copy out/build/x64-debug/bin/CodeGen.exe  <dir>/CodeGen.exe
    copy out/build/x64-debug/bin/libclang.dll <dir>/libclang.dll
    git checkout HEAD -- Tools/CodeGen/CodeGenMain.cpp

(build.py is the only sanctioned build entry point; the copy exists because the next
build overwrites the binary in place.)

Usage
-----
    python regen_codegen_evidence.py --set gates|content --exe <CodeGen.exe> \
        --label "<text>" [--note "<text>"] --out <evidence.txt> [--append]
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
FIX = "openspec/changes/engine-codegen-framework/evidence/fixtures"
INCLUDE = ["-I", "CACore/header"]
DEFAULT_OUTDIR = ".review_tmp/evidence_out"

# (label, header (repo-relative), type, extra argv, expectation)
# expectation: "exit-zero" | "exit-nonzero" | "table=<type> <N>" | "count=<substring>=<N>"
GATES = [
    ("9.42a forward declaration first + marked type", f"{FIX}/fwd_then_marker.h", "Same",
     ["--emit-serialization"], "exit-zero"),
    ("9.42b marker option 'nonserializable'", f"{FIX}/nonserializable.h", "NoSer2",
     ["--emit-serialization"], "exit-nonzero"),
    ("9.42c 'serializable' only inside a comment", f"{FIX}/comment_mention.h", "NoSer4",
     ["--emit-serialization"], "exit-nonzero"),
    ("9.42 control: real serializable option", "Tools/CodeGen/TestEmit.h", "EmitSample",
     ["--emit-serialization"], "exit-zero"),
    ("9.43a text channel: reflection + serialization", "Tools/CodeGen/TestExternal.h",
     "CodeGenExternal", ["--emit-reflection", "--emit-serialization"], "exit-nonzero"),
    ("9.43b AST path: serialization + hash", "Tools/CodeGen/TestEmit.h", "EmitSample",
     ["--emit-serialization", "--emit-hash"], "exit-nonzero"),
]

CONTENT = [
    ("9.50a forward declaration first: field table must have 2 fields",
     f"{FIX}/fwd_then_marker.h", "Same", [], "table=Same 2"),
    ("9.50b serializable = 0 must be refused", f"{FIX}/r31-cases.h", "Zero1",
     ["--emit-serialization"], "exit-nonzero"),
    ("9.50c serializable = false must be refused", f"{FIX}/r31-cases.h", "Zero2",
     ["--emit-serialization"], "exit-nonzero"),
    ("9.50d marker spanning several lines (FIRST marker in its file) must be accepted",
     f"{FIX}/multiline_marker.h", "ML", ["--emit-serialization"], "count=s.serialize(obj.=2"),
    ("9.50e D0 marked, D1 unmarked: D1 must be refused", f"{FIX}/r31-cases.h", "D1",
     ["--emit-serialization"], "exit-nonzero"),
    ("9.50f D0 itself must still be accepted", f"{FIX}/r31-cases.h", "D0",
     ["--emit-serialization"], "exit-zero"),
    ("9.54a an annotation mentioned ONLY inside a comment must not count as an annotation",
     f"{FIX}/comment_marker.h", "NotAnnotated", ["--emit-serialization"], "no-artifact"),
    ("9.54b an unbalanced parenthesis inside a comment must not unbalance the real annotation scan",
     f"{FIX}/comment_paren.h", "Paren", ["--emit-serialization"], "count=s.serialize(obj.=2"),
    ("9.55a an annotation inside a backslash-spliced line comment must not count as an annotation",
     f"{FIX}/linesplice_comment.h", "Spliced", ["--emit-serialization"], "no-artifact"),
    ("9.55b an annotation spelling inside a string literal must not count as an annotation",
     f"{FIX}/string_marker.h", "FromString", ["--emit-serialization"], "no-artifact"),
    ("9.55c a string containing the block-comment opener must not delete a REAL annotation",
     f"{FIX}/string_blockcomment.h", "StrBlock", ["--emit-serialization"], "count=s.serialize(obj.=2"),
    ("9.55d a parenthesis inside a string in an earlier marker must not abandon every marker",
     f"{FIX}/paren_in_string.h", "ParenStr", ["--emit-serialization"], "count=s.serialize(obj.=2"),
    ("9.55e a type that is only DECLARED must not be emitted with zero fields",
     f"{FIX}/only_declared.h", "OnlyDeclared", ["--emit-reflection"], "no-artifact"),
    ("9.55e2 control: a REGISTERED type that is only declared must be refused",
     f"{FIX}/only_declared_registered.h", "OnlyDeclared", ["--emit-reflection"],
     "refuse-no-artifact"),
    ("9.55f an unmarked type after a definition split over three lines must be refused",
     f"{FIX}/split_definition.h", "Split1", ["--emit-serialization"], "exit-nonzero"),
    ("control: EmitSample serialization emits 4 member calls", "Tools/CodeGen/TestEmit.h",
     "EmitSample", ["--emit-serialization"], "count=s.serialize(obj.=4"),
    ("control: Multi in r31-cases.h is CONFOUNDED pre-fix (a PASS here is a false green: the "
     "per-line scan leaks Zero2's options into Multi)", f"{FIX}/r31-cases.h", "Multi",
     ["--emit-serialization"], "count=s.serialize(obj.=2"),
]

R26 = [
    ("9.30 text channel + --emit-serialization", "Tools/CodeGen/TestExternal.h",
     "CodeGenExternal", ["--emit-serialization"], "exit-nonzero"),
    ("9.31 annotation without the serializable option + --emit-serialization",
     f"{FIX}/uisample.h", "NoSer", ["-I", "Tools/CodeGen", "--emit-serialization"],
     "refuse-no-artifact"),
    ("control: EmitSample (carries the serializable option)", "Tools/CodeGen/TestEmit.h",
     "EmitSample", ["--emit-serialization"], "count=s.serialize(obj.=4"),
]

SETS = {"gates": GATES, "content": CONTENT, "r26": R26}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def blob_of() -> str:
    """The worktree source blob at the moment the section was written.

    This is NOT necessarily what the binary was built from — a historical binary is built by checking
    an older revision out, building, and restoring — so the truthful statement of origin comes from
    `--source`, and this line is printed next to it as an independent cross-check.
    """
    src = REPO / "Tools" / "CodeGen" / "CodeGenMain.cpp"
    if not src.exists():
        return "(source not on disk)"
    r = subprocess.run(["git", "hash-object", str(src)], cwd=str(REPO),
                       capture_output=True, text=True)
    return r.stdout.strip() or "(unknown)"


def blob_at(rev: str) -> str:
    """MEASURED source blob of a revision, so the commit<->binary claim has a checkable half."""
    r = subprocess.run(["git", "rev-parse", f"{rev}:Tools/CodeGen/CodeGenMain.cpp"],
                       cwd=str(REPO), capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else f"(git rev-parse failed for {rev!r})"


def run_case(exe: Path, outdir: Path, index: int, case, exe_rel: str) -> dict:
    label, header, type_name, extra, expectation = case
    out_file = outdir / f"case{index}.h"
    if out_file.exists():
        out_file.unlink()
    argv = [str(exe), header, type_name, *INCLUDE, *extra, "-o", str(out_file)]
    # The recorded command line uses a repo-relative exe path when there is one, so the
    # text stays readable; the executed argv uses the real path.
    shown = [exe_rel, header, type_name, *INCLUDE, *extra, "-o", str(out_file)]
    proc = subprocess.run(argv, cwd=str(REPO), capture_output=True)
    stdout = proc.stdout.decode("utf-8", errors="replace")
    stderr = proc.stderr.decode("utf-8", errors="replace")
    artifact = out_file.exists()
    body = out_file.read_text(encoding="utf-8", errors="replace") if artifact else ""

    verdict = ""
    if expectation == "exit-zero":
        verdict = "PASS" if proc.returncode == 0 else "FAIL"
    elif expectation == "exit-nonzero":
        verdict = "PASS" if proc.returncode != 0 else "FAIL"
    elif expectation == "no-artifact":
        # Nothing may be written for this type, and the tool must say WHY. The REASON is checked too:
        # "returncode == 0 and no file" is also true when a REAL annotation was destroyed, which is the
        # opposite of what this case exists to prove (found by the round-32 adversarial review).
        ok = (proc.returncode == 0 and not artifact and "empty whitelist" in stderr)
        verdict = ("PASS" if ok else "FAIL") + f" (artifact={artifact})"
    elif expectation == "refuse-no-artifact":
        # The run must be REFUSED (non-zero), say why, and leave the requested output path untouched.
        ok = proc.returncode != 0 and not artifact and stderr.strip() != ""
        verdict = ("PASS" if ok else "FAIL") + f" (artifact={artifact})"
    elif expectation.startswith("table="):
        want = expectation.split("=", 1)[1]
        verdict = "PASS" if any(l.strip() == want for l in stdout.splitlines()) else "FAIL"
    elif expectation.startswith("count="):
        needle, want = expectation.split("=", 1)[1].rsplit("=", 1)
        got = body.count(needle)
        # The count must come from an artifact written for THIS type: a specialization for a different
        # type of the same name would otherwise satisfy a bare substring count (same review).
        first = body.splitlines()[0] if body else ""
        named = type_name in first
        ok = got == int(want) and named
        verdict = f"{'PASS' if ok else 'FAIL'} (got {got}, artifact names the type: {named})"
    return {
        "label": label, "argv": shown, "exit": proc.returncode, "artifact": artifact,
        "stdout": stdout, "stderr": stderr, "verdict": verdict, "body": body,
    }


def render(meta: dict, results: list[dict]) -> str:
    lines = [f"### {meta['label']}"]
    lines.append("# expectations are the POST-FIX ones: in a BEFORE run a FAIL verdict IS the red state")
    lines.append(f"binary: {meta['exe_rel']}")
    lines.append(f"binary sha256: {meta['exe_sha256']}")
    lines.append(f"built from (DECLARED by the operator, not measured by this script): {meta['source']}")
    if meta.get("source_rev"):
        lines.append(f"measured source blob of {meta['source_rev']}: {meta['source_blob']}")
    lines.append(f"worktree Tools/CodeGen/CodeGenMain.cpp blob at write time: {meta['blob']}")
    if meta.get("note"):
        lines.append(f"note: {meta['note']}")
    lines.append("")
    for r in results:
        lines.append(f"--- {r['label']}")
        lines.append("cmd: " + " ".join(r["argv"]))
        lines.append(f"exit: {r['exit']}")
        lines.append(f"artifact: {r['artifact']}")
        lines.append(f"verdict: {r['verdict']}")
        for name in ("stdout", "stderr"):
            text = r[name]
            lines.append(f"{name}:")
            for line in text.splitlines():
                lines.append("  " + line)
            if not text.strip():
                lines.append("  (empty)")
        if r["artifact"]:
            body = r["body"].splitlines()
            lines.append(f"artifact body (first {min(40, len(body))} of {len(body)} lines):")
            for line in body[:40]:
                lines.append("  " + line)
        lines.append("")
    passed = sum(1 for r in results if r["verdict"].startswith("PASS"))
    lines.append(f"TOTAL {len(results)} cases, {passed} PASS, {len(results) - passed} FAIL")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", dest="set_name", choices=sorted(SETS), required=True)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--label", required=True)
    ap.add_argument("--source", required=True,
                    help="what code state the binary was built from, in words + the source blob hash")
    ap.add_argument("--source-commit", default="",
                    help="optional revision whose Tools/CodeGen/CodeGenMain.cpp blob is then MEASURED "
                         "and printed next to the declared origin")
    ap.add_argument("--note", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--append", action="store_true")
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR)
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.is_absolute():
        exe = (REPO / args.exe).resolve()
    if not exe.exists():
        print(f"ERROR: no such binary: {exe}")
        return 2
    try:
        exe_rel = exe.relative_to(REPO).as_posix()
    except ValueError:
        exe_rel = str(exe)

    outdir = (REPO / args.outdir)
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    cases = SETS[args.set_name]
    results = [run_case(exe, outdir, i, c, exe_rel) for i, c in enumerate(cases)]

    meta = {"label": args.label, "exe_rel": exe_rel, "exe_sha256": sha256(exe),
            "blob": blob_of(), "source": args.source, "note": args.note,
            "source_rev": args.source_commit,
            "source_blob": blob_at(args.source_commit) if args.source_commit else ""}
    text = render(meta, results)

    out_path = Path(args.out)
    if not out_path.is_absolute():
        out_path = REPO / out_path
    with out_path.open("a" if args.append else "w", encoding="utf-8", newline="\n") as f:
        if args.append:
            f.write("\n")
        f.write(text)

    for r in results:
        print(f"  {r['verdict']:>16}  exit={r['exit']}  {r['label']}")
    failed = [r for r in results if not r["verdict"].startswith("PASS")]
    print(f"set={args.set_name} cases={len(results)} failed={len(failed)} -> {out_path}")
    # Exit 0 even when a case is "red": the point of a BEFORE run is to record red states.
    # The verdict column is what a reader checks, so a FAIL here is data, not a script error.
    print("NOTE: FAIL verdicts are intentional in a BEFORE run; they are the red states.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
