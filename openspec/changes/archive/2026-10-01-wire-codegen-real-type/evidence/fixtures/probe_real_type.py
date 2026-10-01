"""Task 3.1 probe: generate for the real engine type `graphics_backend::GPUBufferDescriptor`.

Usage:  python openspec/changes/wire-codegen-real-type/evidence/fixtures/probe_real_type.py [--check]

Without flags it runs the three emitters into %TEMP% and prints exit codes / artifact sizes, which is
the red/green pair tasks 3.1 and 3.3 ask for. With `--check` it only prints the environment summary.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

def _repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "build.py").is_file() and (candidate / "CACore").is_dir():
            return candidate
    raise SystemExit("repo root not found (no build.py + CACore above %s)" % start)


PROJ = _repo_root(Path(__file__).resolve())
sys.path.insert(0, str(PROJ))
import build  # noqa: E402

HEADER = "Interface/RenderInterface/header/GPUBuffer.h"
TYPE = "GPUBufferDescriptor"

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))
build.CODEGEN_ANNOTATED = [(HEADER, TYPE, "Interface", "Interface/RenderInterface/generated")]
env = build._compile_env_by_root(entries)["Interface"]
if "--check" in sys.argv:
    raise SystemExit(0)

exe = PROJ / "out/build/x64-debug/bin/CodeGen.exe"
outdir = Path(tempfile.gettempdir()) / "wire_real_type"
outdir.mkdir(exist_ok=True)
for flag, suffix in (("--emit-reflection", "Reflection"), ("--emit-serialization", "Serialization"),
                     ("--emit-hash", "Hash")):
    out = outdir / f"{TYPE}.{suffix}.h"
    out.unlink(missing_ok=True)
    r = subprocess.run([str(exe), HEADER, TYPE, *env, flag, "-o", str(out)],
                       cwd=str(PROJ), capture_output=True, text=True)
    first = (r.stderr.strip().splitlines() or ["(stderr 空)"])[0]
    print(f"  {flag:22s} exit={r.returncode} artifact={out.exists()} "
          f"bytes={out.stat().st_size if out.exists() else 0}  {first[:80]}")
