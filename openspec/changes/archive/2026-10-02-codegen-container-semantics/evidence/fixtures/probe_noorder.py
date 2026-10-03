"""Task 4.1 (1) probe: the sorting side of an unordered container must have a total order.

The comparison criterion MUST accept `operator<=>`: the engine's real key types (`NameHash`, `PathHash`,
`CAHash::result_type`) only define `<=>`, so a criterion that looks for `operator<` alone would reject
every one of them (that false rejection is produced by the mutation step in probe_ordering.py).

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_noorder.py
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
#include <unordered_map>
#include <unordered_set>
namespace probe_noorder
{
    struct NoOrder                       // no operator< and no operator<=>
    {
        unsigned v;
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
    ("A_set", "Set", "std::unordered_set<NoOrder> items;"),
    ("B_mapkey", "MapKey", "std::unordered_map<NoOrder, unsigned> byKey;"),
    ("C_control", "Plain", "std::unordered_set<unsigned> us; std::unordered_map<unsigned, unsigned> um;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Set", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_noorder_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} ({member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:200])
