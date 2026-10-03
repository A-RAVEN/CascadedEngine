"""Task 5.1 probe: wrapper elements (`NameHash`) must be ACCEPTED (recorded, never refused).

Usage:  python openspec/changes/codegen-container-semantics/evidence/fixtures/probe_wrapper.py

The contract in the design (D3) is that a "wrapper" is only *recorded*: the old runtime unpacks it with
`managed_wrapper_traits`, so `vector<NameHash>` / `unordered_map<NameHash, X>` must keep working. The
opposite implementation ("wrapper counts as opaque -> refuse") would reject the engine's real types.
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
#include "Hasher.h"
#include <vector>
#include <unordered_map>
namespace probe_wrapper
{
    CA_REFLECT(serializable)
    struct Holder
    {
        std::vector<NameHash>                  names;
        std::unordered_map<NameHash, unsigned> byName;
        unsigned                               plain;
    };
}
"""

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [("x", "Holder", "Interface/RenderInterface")]
env = build._compile_env_by_root(entries)["Interface/RenderInterface"]
# KNOWN BLOCKER (recorded in tasks.md, task 5.1): `Hasher.h` pulls `<sha256.h>`, which is not on the
# Interface module's include path, so this fixture stops at exit 6 with "the compile environment fed
# to this tool is incomplete". Only the module roots present in compile_commands.json can be used here.

path = Path(tempfile.gettempdir()) / "probe_wrapper.h"
path.write_text(FIXTURE, encoding="utf-8")

for flag in ("--emit-serialization", "--emit-hash"):
    r = subprocess.run([str(PROJ / "out/build/x64-debug/bin/CodeGen.exe"), str(path), "Holder", flag, *env],
                       cwd=str(PROJ), capture_output=True, text=True)
    print(f"--- {flag} exit={r.returncode}")
    if r.stderr.strip():
        print("stderr:", r.stderr.strip()[:300])
    for ln in r.stdout.splitlines():
        if "obj.names" in ln or "obj.byName" in ln:
            print("emitted:", ln.strip())
