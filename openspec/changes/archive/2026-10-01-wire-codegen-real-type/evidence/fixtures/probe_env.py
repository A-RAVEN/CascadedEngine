"""Probe for tasks 2.1 / 2.2 / 2.3 — the compile-environment derivation for real modules.

Usage:  python openspec/changes/wire-codegen-real-type/evidence/fixtures/probe_env.py

It calls build.py's real helpers, so it measures the shipping code path:
  2.1  include dirs are the UNION of the module's compile units (counted per unit vs union)
  2.2  a module whose units disagree about macros is REFUSED (measured: `Test` has two macro sets)
  2.3  the unit's forced include / PCH (`/FI` + `/Yu`) is kept as `-include <file>`
"""
import json
import sys
from pathlib import Path

def _repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "build.py").is_file() and (candidate / "CACore").is_dir():
            return candidate
    raise SystemExit("repo root not found (no build.py + CACore above %s)" % start)


PROJ = _repo_root(Path(__file__).resolve())
sys.path.insert(0, str(PROJ))
import build  # noqa: E402

entries = json.loads((PROJ / "out/build/x64-debug/compile_commands.json").read_text(encoding="utf-8"))

print("=== 2.1 include 目录：单条编译单元 vs 整个模块的并集 ===")
for root in ("CACore", "Interface", "VulkanRenderBackendNew", "Test", "Tools/CodeGen"):
    tus = build._tus_for_root(entries, root)
    per_unit = []
    for rel in tus:
        cmd = next((e.get("command", "") for e in entries
                    if (build._rel_to_repo(e.get("file", "")) or "") == rel), "")
        per_unit.append(len(build._msvc_env_parts(cmd)["includes"]))
    union = len(build._env_for_root.__wrapped__(entries, root)["includes"]) if hasattr(
        build._env_for_root, "__wrapped__") else None
    # measure the union without the printing/refusing path
    incs: list[str] = []
    for rel in tus:
        cmd = next((e.get("command", "") for e in entries
                    if (build._rel_to_repo(e.get("file", "")) or "") == rel), "")
        for i in build._msvc_env_parts(cmd)["includes"]:
            if i not in incs:
                incs.append(i)
    print(f"  {root:24s} 编译单元={len(tus):3d} 单条最多={max(per_unit) if per_unit else 0:3d} "
          f"并集={len(incs):3d}")

print("\n=== 2.2 宏集合不唯一的模块必须被拒绝 ===")
for root in ("Interface", "CACore", "Test"):
    try:
        build._env_for_root(entries, root)
        print(f"  {root:12s} -> 通过")
    except build.EnvError as exc:
        print(f"  {root:12s} -> 拒绝：{str(exc)[:110]}")

print("\n=== 2.3 预编译头的强制包含被保留 ===")
import io
import contextlib
for root in ("Interface", "CACore"):
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            args = build._env_for_root(entries, root)
    except build.EnvError as exc:
        print(f"  {root:12s} -> 该模块被拒绝（见 2.2）：{str(exc)[:60]}")
        continue
    forced = [args[i + 1] for i, a in enumerate(args) if a == "-include"]
    print(f"  {root:12s} -> {len(args)} 个参数；-include {[Path(f).name for f in forced]}；"
          f"摘要：{buf.getvalue().strip()}")
