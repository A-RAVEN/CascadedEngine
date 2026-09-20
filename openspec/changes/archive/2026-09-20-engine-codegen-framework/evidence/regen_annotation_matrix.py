#!/usr/bin/env python3
"""Task 9.63: one PROPERTY x many inputs x all three annotation channels, generated and executed.

Why this file exists. Four review rounds in a row each ended with "the fix is green" and the next round
found the same property broken through a neighbouring input, because every round wrote its criterion
against the ONE fixture it had just invented. A criterion bound to a fixture only proves "this fixture
passes"; it says nothing about the property. This script instead states the property once and applies it
to a matrix:

    property:  an annotation that the preprocessor did NOT perform must not make a type deliverable
    inputs:    the ways a spelling can sit in something that is not code (rows)
    channels:  CA_REFLECT / CA_CODEGEN_REGISTER / CA_REFLECTION (columns)

Every cell is one command with its real exit code and whether an artifact was written; the positive
column at the bottom checks that a REAL annotation still works, so the matrix cannot be vacuously green.

Usage:
    python regen_annotation_matrix.py --exe <CodeGen.exe> --label "<text>" --out <evidence file> [--append]
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
HERE = pathlib.Path(__file__).resolve().parent
FIX = HERE / "fixtures" / "matrix"
INCLUDE = ["-I", "CACore/header"]

# The three annotation channels. Each entry: (id, invocation macro text with a placeholder, real use).
CHANNELS = [
    ("CA_REFLECT", "CA_REFLECT(serializable)", "CA_REFLECT(serializable)"),
    ("CA_CODEGEN_REGISTER", 'CA_CODEGEN_REGISTER("probe::Target");', 'CA_CODEGEN_REGISTER("probe::Target");'),
    ("CA_REFLECTION", "CA_REFLECTION(Target, a, b);", "CA_REFLECTION(Target, a, b);"),
]

# Only CA_REFLECTION needs a local definition: CodeGenMeta.h (included by every fixture below) already
# defines the other two, and redefining a macro with different text is a compile error. The first version
# of this file defined all three, which made the whole CA_REFLECT/CA_CODEGEN_REGISTER half of the matrix
# green for the wrong reason: the fixture never parsed, so of course no artifact was written.
MACRO_DEFS = {
    "CA_REFLECT": "",
    "CA_CODEGEN_REGISTER": "",
    "CA_REFLECTION": "#define CA_REFLECTION(Type, ...) static_assert(true, \"CA_REFLECTION:\" #Type)",
}

# Rows: (id, how the spelling is wrapped, expected-to-be-inactive?). {A} = the invocation text.
ROWS = [
    ("line comment",        "// {A}\n", False),
    ("block comment",       "/* {A} */\n", False),
    ("string literal",      'static const char* kText = "{A}";\n', False),
    ("raw string literal",  'static const char* kRaw = R"x({A})x";\n', False),
    ("spliced line comment","// note \\\n{A}\n", False),
    ("#if 0",               "#if 0\n{A}\n#endif\n", False),
    ("#if (0)",             "#if (0)\n{A}\n#endif\n", False),
    ("#if false",           "#if false\n{A}\n#endif\n", False),
    ("#ifdef undefined",    "#ifdef NEVER_DEFINED_ANYWHERE\n{A}\n#endif\n", False),
    ("unused #define body", "#define REG(T) {A}\n", False),
    ("#elif 0",             "#if SOME_UNDEFINED_CONDITION\nint x;\n#elif 0\n{A}\n#endif\n", False),
]


def fixture_text(channel: str, row_id: str, wrapper: str) -> str:
    invocation = dict((c[0], c[1]) for c in CHANNELS)[channel].replace(";", "")
    body = wrapper.replace("{A}", invocation)
    return (
        '#pragma once\n#include "CodeGenMeta.h"\n'
        + MACRO_DEFS[channel] + "\n"
        + "namespace probe {\n"
        + "    struct Target { int a; float b; };\n"
        + body
        + "}\n"
    )


def positive_text(channel: str) -> str:
    """A REAL invocation, placed the way that channel is actually written.

    CA_REFLECT sits ABOVE the type it marks (that is how every real use looks, e.g. TestEmit.h), while
    the other two name the type inside the invocation and therefore follow it. Getting this wrong makes
    the positive cell fail for a reason that has nothing to do with the property under test.
    """
    invocation = dict((c[0], c[2]) for c in CHANNELS)[channel]
    head = '#pragma once\n#include "CodeGenMeta.h"\n' + MACRO_DEFS[channel] + "\nnamespace probe {\n"
    if channel == "CA_REFLECT":
        return head + "    " + invocation + "\n    struct Target { int a; float b; };\n}\n"
    return head + "    struct Target { int a; float b; };\n    " + invocation + "\n}\n"


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(exe: pathlib.Path, fixture: pathlib.Path, out: pathlib.Path, exe_rel: str) -> dict:
    if out.exists():
        out.unlink()
    argv = [str(exe), str(fixture), "Target", *INCLUDE, "--emit-reflection", "-o", str(out)]
    p = subprocess.run(argv, cwd=str(REPO), capture_output=True)
    err = p.stderr.decode("utf-8", "replace").strip()
    return {
        "exit": p.returncode,
        "artifact": out.exists(),
        "stderr": err.splitlines()[0] if err else "",
        "cmd": " ".join([exe_rel, fixture.relative_to(REPO).as_posix(), "Target", *INCLUDE,
                         "--emit-reflection", "-o", out.name]),
    }


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--label", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--append", action="store_true")
    args = ap.parse_args()

    exe = pathlib.Path(args.exe)
    if not exe.is_absolute():
        exe = (REPO / args.exe).resolve()
    if not exe.exists():
        print(f"ERROR: no such binary: {exe}")
        return 2
    exe_rel = exe.relative_to(REPO).as_posix() if str(exe).startswith(str(REPO)) else str(exe)

    FIX.mkdir(parents=True, exist_ok=True)
    outdir = REPO / ".review_tmp" / "matrix_out"
    outdir.mkdir(parents=True, exist_ok=True)

    lines = [f"### {args.label}",
             f"binary: {exe_rel}",
             f"binary sha256: {sha256(exe)}",
             f"built from (DECLARED): {args.source}",
             "property: an annotation the preprocessor did NOT perform must not make a type deliverable",
             ""]
    failures = 0
    for channel, _, _ in CHANNELS:
        lines.append(f"--- channel {channel}")
        for row_id, wrapper, _ in ROWS:
            name = f"{channel.lower()}_{row_id.replace(' ', '_').replace('#', 'if').replace('(', '').replace(')', '')}.h"
            fixture = FIX / name
            fixture.write_text(fixture_text(channel, row_id, wrapper), encoding="utf-8", newline="\n")
            r = run_case(exe, fixture, outdir / (name + ".out.h"), exe_rel)
            # The property is "the spelling did not register the type". A missing artifact on its own is
            # NOT evidence — a file that failed to parse also writes nothing — so the cell only counts as
            # measured when the run reached the annotation gate and found nothing to emit.
            # "Measured" means the run reached the annotation gate: it either emitted (so it certainly got
            # there), or it reported an empty annotation list, or it refused this one type as unannotated.
            measured = (r["artifact"]
                        or (r["exit"] == 0 and "no annotated types" in r["stderr"])
                        or (r["exit"] == 4 and "is not annotated" in r["stderr"]))
            ok = (not r["artifact"]) and measured
            failures += 0 if ok else 1
            why = ""
            if not ok:
                why = ("  (cell measured nothing: the run never reached the gate)"
                       if not measured else "  (the spelling REGISTERED the type: an artifact was written)")
            lines.append(f"  {row_id:22s} {'OK  ' if ok else 'RED '} exit={r['exit']} artifact={r['artifact']}"
                         + (f"  stderr={r['stderr'][:90]}" if r["stderr"] else "") + why)
        # positive control: a REAL invocation must still be delivered
        fixture = FIX / f"{channel.lower()}_positive.h"
        fixture.write_text(positive_text(channel), encoding="utf-8", newline="\n")
        r = run_case(exe, fixture, outdir / (fixture.name + ".out.h"), exe_rel)
        ok = r["artifact"]
        failures += 0 if ok else 1
        lines.append(f"  {'REAL USE (positive)':22s} {'OK  ' if ok else 'RED '} exit={r['exit']} artifact={r['artifact']}")
        lines.append("")
    lines.append(f"matrix: {len(ROWS)} spellings x {len(CHANNELS)} channels + {len(CHANNELS)} positive cells; "
                 f"cells whose property does NOT hold: {failures}")
    lines.append("")

    text = "\n".join(lines)
    out_path = pathlib.Path(args.out)
    if not out_path.is_absolute():
        out_path = REPO / out_path
    with out_path.open("a" if args.append else "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    for line in text.splitlines():
        if " RED " in line or line.startswith("matrix:") or line.startswith("--- channel"):
            print(line)
    print(f"-> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
