"""Task 3.2 (1) probe: a container whose ELEMENT is a struct holding an unordered container.

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_refuse_element.py

Writes both fixtures to %TEMP% (never into the repo), runs the sidecar with the
`Interface/RenderInterface` compile environment derived by build.py, and prints the exit code.

Case A: `std::vector<Inner>` where `Inner` holds a `std::unordered_map`  -> must be REFUSED.
Case B: `std::vector<Plain>` where `Plain` holds only scalars and an ordered container -> must be
        accepted (the control: the refusal must not swallow ordinary struct elements).
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

PROJ = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(PROJ))
import build  # noqa: E402

BODY = """#pragma once
#include "CodeGenMeta.h"
#include <vector>
#include <map>
#include <unordered_map>
namespace probe_refuse
{
    struct Inner
    {
        unsigned                               a;
        std::unordered_map<unsigned, unsigned> m;   // the unreachable unordered container
    };

    struct Plain
    {
        unsigned                 a;
        std::map<unsigned, int>  ordered;           // ordered: no canonical-order problem
    };

    CA_REFLECT(serializable)
    struct %s
    {
        %s
        unsigned plain;
    };
}
"""

CASES = [
    ("A_refused", "Container", "std::vector<Inner> items;"),
    ("B_allowed", "ContainerOfPlain", "std::vector<Plain> items;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Container", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_refuse_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} (element = {member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip())
    if r.stdout.strip():
        print("stdout:", r.stdout.strip()[:400])
