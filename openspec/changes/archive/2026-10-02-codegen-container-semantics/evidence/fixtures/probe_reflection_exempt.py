"""Task 4.2 probe: the spec says that for a reflection-only request none of the five rejections apply.
This runs `--emit-reflection` and `--emit-serialization` over one fixture per rejection shape, so the
spec's exemption list can be compared line by line.

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_reflection_exempt.py
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
#include <CASTL/CASharedPtr.h>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace probe_exempt
{
    struct NoOrder { unsigned v; };
    struct NoDefault { explicit NoDefault(int v) : value(v) {} int value; };
    struct Inner { unsigned a; std::unordered_map<unsigned, unsigned> m; };
    struct Plain { unsigned a; };

    CA_REFLECT(serializable)
    struct %s
    {
        %s
        unsigned plain;
    };
}
"""

CASES = [
    ("bitpacked", "Bit", "std::vector<bool> flags;"),
    ("multikey", "Multi", "std::multimap<unsigned, unsigned> mm;"),
    ("pointer_element", "Ptr", "std::vector<int*> ptrs;"),
    ("no_default_ctor", "NoDef", "std::vector<NoDefault> items;"),
    ("no_ordering", "NoOrd", "std::unordered_set<NoOrder> items;"),
    ("struct_with_unordered", "StructElem", "std::vector<Inner> items;"),
    ("control", "Ok", "std::vector<unsigned> vec; std::map<unsigned, unsigned> omap;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Bit", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_exempt_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    row = []
    for flag in ("--emit-reflection", "--emit-serialization"):
        r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                            flag, *env], cwd=str(PROJ), capture_output=True, text=True)
        row.append(f"{flag.replace('--emit-', '')}={r.returncode}")
    print(f"--- {name:22s} {' '.join(row)}")
