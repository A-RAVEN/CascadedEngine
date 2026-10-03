"""Task 2.1 probe: does the tool recognize containers structurally and report element facts?

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_containers.py

Writes the fixture to %TEMP% (never into the repo), runs the sidecar with the `Interface/RenderInterface`
compile environment derived by build.py, and prints the field table (no --emit flag).
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

PROJ = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(PROJ))
import build  # noqa: E402

FIXTURE = """#pragma once
#include "CodeGenMeta.h"
#include <CASTL/CAContainerBase.h>
#include <CASTL/CASharedPtr.h>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
namespace probe
{
    CA_REFLECT(serializable)
    struct Containers
    {
        std::vector<unsigned>                     vec;
        std::unordered_map<unsigned, unsigned>    umap;
        std::map<unsigned, unsigned>              omap;
        std::set<unsigned>                        oset;
        std::unordered_set<unsigned>              uset;
        std::vector<std::vector<int>>             nested;
        std::vector<int*>                         ptrs;
        std::vector<bool>                         bits;
        std::vector<castl::shared_ptr<int>>       shared;
        unsigned                                  plain;
        int*                                      rawptr;
    };
}
"""

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Containers", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]

path = Path(tempfile.gettempdir()) / "probe_containers.h"
path.write_text(FIXTURE, encoding="utf-8")
r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), "Containers", *env],
                   cwd=str(PROJ), capture_output=True, text=True)
print(f"exit={r.returncode}")
print(r.stdout.strip())
if r.stderr.strip():
    print("stderr:", r.stderr.strip()[:300])
