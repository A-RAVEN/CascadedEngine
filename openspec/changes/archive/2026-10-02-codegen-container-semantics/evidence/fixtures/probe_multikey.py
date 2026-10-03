"""Task 4.1 (3) probe: duplicate-key containers (`multimap` / `multiset` / unordered variants) have no
canonical order among equal keys, so the serialization and hash emitters must REFUSE them.

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_multikey.py
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
#include <map>
#include <set>
#include <unordered_set>
namespace probe_multikey
{
    CA_REFLECT(serializable)
    struct %s
    {
        %s
        unsigned plain;
    };
}
"""

CASES = [
    ("A_multimap", "Multi", "std::multimap<unsigned, unsigned> mm;"),
    ("B_multiset", "MultiSet", "std::unordered_multiset<unsigned> ms;"),
    ("C_control", "Single", "std::map<unsigned, unsigned> omap; std::unordered_set<unsigned> uset;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Multi", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_multikey_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} ({member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:260])
