"""Task 4.1 (4) probe: a container whose KEY or ELEMENT is not default-constructible must be refused
(the generated reader builds a value with `E e{}` before reading into it).

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_nodefault.py
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
#include <vector>
namespace probe_nodefault
{
    struct NoDefault
    {
        explicit NoDefault(int v) : value(v) {}
        int value;
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
    ("A_element", "Element", "std::vector<NoDefault> items;"),
    ("B_key", "Key", "std::map<NoDefault, unsigned> byKey;"),
    ("C_control", "Plain", "std::vector<unsigned> vec; std::map<unsigned, unsigned> omap;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Element", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_nodefault_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} ({member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:200])
