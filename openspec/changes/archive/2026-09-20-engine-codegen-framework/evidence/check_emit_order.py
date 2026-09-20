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
"""Task 9.5a check: emission order == declaration order (no offset-based sort).

The old code sorted fields by `offsetBytes`, i.e. it derived the emission order from a
config-sensitive parse whose offsets can collapse to 0. The property we now assert is the
one the emitters actually need: output order follows the declaration order in the header.

Usage: python .review_tmp/check_emit_order.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]  # 9.46: archived under <repo>/openspec/changes/<change>/evidence
SID = ROOT / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"
OUT = ROOT / ".review_tmp" / "order"
OUT.mkdir(parents=True, exist_ok=True)

CASES = [
    ("Tools/CodeGen/TestStruct.h", "CodeGenSample"),
    ("Tools/CodeGen/TestNested.h", "CodeGenOuter"),
    ("Tools/CodeGen/TestEmit.h", "EmitSample"),
]

ok = True
for hdr, ty in CASES:
    out_file = OUT / f"{ty}.h"
    r = subprocess.run([str(SID), hdr, ty, "-I", "CACore/header",
                        "--emit-reflection", "-o", str(out_file)],
                       cwd=str(ROOT), capture_output=True, text=True)
    src = (ROOT / hdr).read_text(encoding="utf-8")
    body = src[src.index("struct " + ty):]
    body = body[:body.index("};")]
    # member declarations at struct-body depth 1 (skip methods / bases)
    decl = re.findall(r"^\s{4}(?:[\w:<>,\s\*&]+?)\b(\w+)\s*(?:=|;)", body, re.M)
    emitted = (re.findall(r'\{ "(\w+)"', out_file.read_text(encoding="utf-8"))
               if out_file.exists() else [])
    match = emitted == decl
    ok &= match and r.returncode == 0
    print(f"{ty:16} exit={r.returncode}  emitted={emitted}  declared={decl}  "
          f"ORDER_MATCHES={match}")

print()
print("RESULT:", "PASS - emission follows declaration order" if ok else "FAIL")
sys.exit(0 if ok else 1)
