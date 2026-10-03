"""Task 4.1 (2) probe: a container whose ELEMENT is a raw / managed pointer must be REFUSED for
serialization and hash (the old runtime silently skips such elements, so data disappears).

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_opaque.py
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
#include <vector>
namespace probe_opaque
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
    ("A_rawptr", "Raw", "std::vector<int*> ptrs;"),
    ("B_managed", "Managed", "std::vector<castl::shared_ptr<int>> shared;"),
    ("C_control", "Plain", "std::vector<unsigned> vec;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Raw", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, type_name, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_opaque_{name}.h"
    path.write_text(BODY % (type_name, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), type_name,
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} ({member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:200])
