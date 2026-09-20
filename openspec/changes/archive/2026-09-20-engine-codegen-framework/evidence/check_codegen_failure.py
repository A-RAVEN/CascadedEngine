# --- Bootstrap (task 9.46) ---------------------------------------------------------------
# This script was archived out of .review_tmp/ for the record. Resolve the repository root from
# this file's own location (not from the current directory) and put it on sys.path, so a clean
# checkout can run it from anywhere.
import os as _os, sys as _sys
from pathlib import Path as _Path
_here = _Path(__file__).resolve()
for _p in [_here.parent, *_here.parents]:
    if (_p / "build.py").exists() and (_p / ".git").exists():
        _os.chdir(_p)
        _sys.path.insert(0, str(_p))
        try:
            _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
            _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
        break
# --- end bootstrap ----------------------------------------------------------------------
"""Executable check for task 9.9: a codegen failure must be *returned*, not swallowed.

Before the fix, codegen() returned None and every abnormal path printed [WARN] and continued,
so the build still printed BUILD SUCCESSFUL and exited 0 (indistinguishable from success).

Usage: python .review_tmp/check_codegen_failure.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]  # 9.46: archived under <repo>/openspec/changes/<change>/evidence
sys.path.insert(0, str(ROOT))
import build  # noqa: E402

build.BUILD_DIR = ROOT / "out" / "build" / "x64-debug"

print("== case 1: manifest names a type the fixture does not annotate (sidecar must exit != 0) ==")
build.CODEGEN_ANNOTATED = [("Tools/CodeGen/TestEmit.h", "NotAnnotatedAnywhere", "Tools/CodeGen")]
failures = build.codegen()
print(f"codegen() returned: {failures!r}")
assert failures, "EXPECTED a non-empty failure list -- the defect (silent swallow) is still present"
assert any("exit" in f for f in failures), failures

print()
print("== case 2: empty manifest is a legitimate configuration, not a failure ==")
build.CODEGEN_ANNOTATED = []
failures = build.codegen()
print(f"codegen() returned: {failures!r}")
assert failures == [], failures

print()
print("== case 3: missing compile_commands.json must be a failure ==")
real = build.BUILD_DIR / "compile_commands.json"
moved = real.with_suffix(".json.bak")
real.rename(moved)
try:
    build.CODEGEN_ANNOTATED = [("Tools/CodeGen/TestEmit.h", "EmitSample", "Tools/CodeGen")]
    failures = build.codegen()
finally:
    moved.rename(real)
print(f"codegen() returned: {failures!r}")
assert failures and "compile_commands.json not found" in failures[0], failures

print()
print("ALL 3 CASES PASSED: failures are returned (main() turns them into exit 1),")
print("empty manifest stays non-fatal.")
