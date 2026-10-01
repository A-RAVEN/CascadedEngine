"""Probe for the two defects this change fixes (tasks 1.1 / 1.2).

Usage:  python openspec/changes/wire-codegen-real-type/evidence/fixtures/probe_defects.py

It calls build.py's real helpers (imported, not re-implemented), so it measures the shipping code path:

  A) module-root matching — before: a SUBSTRING test picked assimp's `CInterfaceIOWrapper.cpp` for the
     root `Interface`; after: path-segment matching against the repo root, build tree excluded.
  B) the empty-annotation path — before: a syntax error / missing include / undefined marker macro all
     exited 0 with "no annotated types"; after: they exit non-zero and name the diagnostic.
"""
import json
import subprocess
import sys
from pathlib import Path

def _repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "build.py").is_file() and (candidate / "CACore").is_dir():
            return candidate
    raise SystemExit("repo root not found (no build.py + CACore above %s)" % start)


PROJ = _repo_root(Path(__file__).resolve())
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ))
import build  # noqa: E402  (guarded by __main__)

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))

print("=== 缺陷 A：模块根是怎么匹配的 ===")
for root in ("Interface", "CACore", "Tools/CodeGen"):
    got = build._tus_for_root(entries, root)
    print(f"  根 {root!r}: {len(got)} 个编译单元；前 3 个: {got[:3]}")
allrel = [build._rel_to_repo(e.get("file", "")) for e in entries]
inbuild = [r for r in allrel if r and r.startswith("out/")]
print(f"  编译单元总数 {len(entries)}；落在 out/ 构建树里的 {len(inbuild)}（这些一律不算本仓库模块）")
build.CODEGEN_ANNOTATED = [("Interface/RenderInterface/header/GPUBuffer.h", "GPUBufferDescriptor",
                            "Interface")]
env = build._compile_env_by_root(entries).get("Interface", [])
print(f"  Interface 推导出的参数个数: {len(env)}；含 -ICACore/header: "
      f"{any(a.lower().replace(chr(92), '/').endswith('cacore/header') for a in env)}；"
      f"含 assimp 宏: {any('ASSIMP' in a.upper() for a in env)}")

print("\n=== 缺陷 B：解析失败被报告成「没有标注」 ===")
exe = PROJ / "out/build/x64-debug/bin/CodeGen.exe"
out = HERE / "_probe_out.h"
expect = {"valid_no_annotations.h": 0, "syntax_error.h": 6, "missing_include.h": 6,
          "undefined_marker.h": 6}
for name, ty in (("valid_no_annotations.h", "Plain"), ("syntax_error.h", "Broken"),
                 ("missing_include.h", "Fine"), ("undefined_marker.h", "Nope")):
    out.unlink(missing_ok=True)
    r = subprocess.run([str(exe), str((HERE / name).relative_to(PROJ)), ty, "-ICACore/header",
                        "--emit-reflection", "-o", str(out)], cwd=str(PROJ),
                       capture_output=True, text=True)
    got = r.returncode
    first = (r.stderr.strip().splitlines() or ["(stderr 空)"])[0]
    verdict = "OK" if got == expect[name] else f"MISMATCH (expected {expect[name]})"
    print(f"  {name:24s} exit={got} artifact={out.exists()} {verdict}  {first[:70]}")
out.unlink(missing_ok=True)
