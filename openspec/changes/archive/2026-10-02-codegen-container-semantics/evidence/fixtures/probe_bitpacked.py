"""Task 2.1 probe: the bit-packed container (`vector<bool>`) must be REFUSED, not silently emitted.

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_bitpacked.py

Two cases, both written to %TEMP% (never into the repo):
  A: `std::vector<bool>`            -- the shape that satisfies the structural criterion but not the
                                       old runtime's `for (auto& item : object)` loop.
  B: `castl::vector<bool>`          -- the same thing spelled with the engine's own alias.
Run it BEFORE the guard to see the red state (exit 0, emitted `s.serialize(obj.flags);`) and AFTER to
see the refusal.
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
#include <CASTL/CAContainerBase.h>
#include <CASTL/CAVector.h>
#include <vector>
namespace probe_bits
{
    %s
    struct Bits
    {
        %s
        unsigned plain;
    };
}
"""

CASES = [
    ("A_std", "CA_REFLECT(serializable)", "std::vector<bool> flags;"),
    ("B_castl", "CA_REFLECT(serializable)", "castl::vector<bool> flags;"),
]

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Bits", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

for name, marker, member in CASES:
    path = Path(tempfile.gettempdir()) / f"probe_bits_{name}.h"
    path.write_text(BODY % (marker, member), encoding="utf-8")
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), "Bits",
                        "--emit-serialization", *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {name} ({member}) exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:400])
    for ln in r.stdout.splitlines():
        if "obj.flags" in ln:
            print("emitted:", ln.strip())
