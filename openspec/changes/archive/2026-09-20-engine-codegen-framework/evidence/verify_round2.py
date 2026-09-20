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
"""Round-2 criteria: the six rework tasks from the round-1 adversarial pass.

Run with --root <dir> to evaluate a pre-fix snapshot (produces the RED evidence).

Usage:
  python .review_tmp/verify_round2.py                 # GREEN expected
  python .review_tmp/verify_round2.py --root .review_tmp/r2pre   # RED expected
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]  # 9.46: archived under <repo>/openspec/changes/<change>/evidence
ALT = None
if "--root" in sys.argv:
    ALT = (ROOT / sys.argv[sys.argv.index("--root") + 1]).resolve()

REL = {
    "tasks": "openspec/changes/engine-codegen-framework/tasks.md",
    "design": "openspec/changes/engine-codegen-framework/design.md",
    "proposal": "openspec/changes/engine-codegen-framework/proposal.md",
    "spec": "openspec/changes/engine-codegen-framework/specs/codegen-framework/spec.md",
}


def read(key: str) -> str:
    if ALT is not None:
        p = ALT / Path(REL[key]).name
        return p.read_text(encoding="utf-8") if p.exists() else ""
    return (ROOT / REL[key]).read_text(encoding="utf-8")


def _strip_task_blocks(text: str) -> str:
    """Remove §9.17–§9.22 task descriptions and the Review-Log findings lists.

    Those blocks *describe* the defects (and therefore quote the bad forms); a criterion
    must judge the product, not the paragraphs that name the problem.
    """
    out = text
    for marker in ("- [ ] 9.8 ", "- [ ] 9.17 ", "- [ ] 9.18 ", "- [ ] 9.19 ", "- [ ] 9.20 ",
                   "- [ ] 9.21 ", "- [ ] 9.22 ", "- [ ] 9.23 ", "- [ ] 9.24 ", "- [ ] 9.25 ",
                   "- [ ] 9.26 ", "- [ ] 9.27 ", "- [ ] 9.28 "):
        while marker in out:
            i = out.index(marker)
            j = out.find("\n- [", i + 1)
            out = out[:i] + (out[j:] if j != -1 else "")
    # The Review Log is *descriptive/historical*: it quotes the rejected wording while
    # recording that it was fixed. Judging product claims there yields false positives
    # (measured 2026-09-12) — see §9.28 for why these string criteria are being replaced.
    if "## Review Log" in out:
        out = out.split("## Review Log")[0]
    return out


T = {k: read(k) for k in REL}
T_PRODUCT = {k: _strip_task_blocks(v) for k, v in T.items()}
results = []


def check(name: str, ok: bool, evidence: str = "") -> None:
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    for line in evidence.strip().splitlines()[:8]:
        print(f"        {line}")


def hits(text: str, pattern: str) -> list[str]:
    return [l.strip() for l in text.splitlines() if re.search(pattern, l)]


# --- 9.19 scope sync -----------------------------------------------------------------
WHITELIST = ("不在本 change", "不在实现范围", "后续阶段", "后续 task", "扩展位",
             "仅以注释预留", "Phase 3", "C# 绑定阶段", "引文", "已作废", "收窄")
h = []
for k in ("tasks", "design", "proposal"):
    for line in T_PRODUCT[k].splitlines():
        if re.search(r"方法 ?/ ?枚举 ?/ ?基类|methods\(\)|enumInfo\(\)|baseClass\(\)", line):
            h.append((k, line.strip()))
# spec: judge per Scenario block — a block is OK if any of its lines carries the scope marker
for block in T_PRODUCT["spec"].split("#### Scenario:")[1:]:
    if re.search(r"方法 ?/ ?枚举 ?/ ?基类|methods\(\)|enumInfo\(\)|baseClass\(\)", block):
        first = block.strip().splitlines()[0][:60]
        if any(m in block for m in WHITELIST):
            h.append(("spec(ok-block)", first))
        else:
            h.append(("spec(OFF)", first))
off = [x for x in h if not (any(m in x[1] for m in WHITELIST) or x[0].endswith("(ok-block)"))]
check("9.19 字段-only 收窄已在全仓同类站点同步", not off,
      f"hits={len(h)} un-whitelisted={len(off)}\n"
      + "\n".join(f"  OFF {k}: {l[:120]}" for k, l in off)
      + "\n" + "\n".join(f"  ok  {k}: {l[:80]}" for k, l in h if (k, l) not in off))

# --- 9.20 drift ----------------------------------------------------------------------
spec_req5 = T["spec"].split("### Requirement: 生成源码以显式源清单纳入构建")[-1]
check("9.20 漂移方向已声明为「不在本 change 实现范围」",
      "不在本 change 实现范围" in spec_req5, "")
check("9.20 同段不再保留未加标注的规范性漂移分句",
      not hits(spec_req5, r"与注解漂移（字段增删）→|均非静默"),
      "\n".join(hits(spec_req5, r"与注解漂移（字段增删）→|均非静默")) or "(none)")
check("9.20 不再用「无演练对象」这类弱措辞",
      not hits(spec_req5, r"无演练对象"),
      "\n".join(hits(spec_req5, r"无演练对象")) or "(none)")
check("9.20 归属不指向已作废的 §9.2",
      not hits(spec_req5, r"§9\.2"),
      "\n".join(hits(spec_req5, r"§9\.2")) or "(none)")

# --- 9.21 MCP phrasing ---------------------------------------------------------------
bad = [(i, l.strip()) for i, l in enumerate(T_PRODUCT["tasks"].splitlines(), 1)
       if "不可回溯" in l
       # an occurrence inside a correction note is a transcription of the rejected wording,
       # not a prescription: exempt only when the same line says so.
       and not any(m in l for m in ("更正", "互斥", "禁止", "不得", "错的", "本身是错的"))]
check("9.21 不用「不可回溯」这类不可证伪措辞", not bad,
      "\n".join(f"  line {i}: {l[:130]}" for i, l in bad) or "(none)")

# --- 9.22 --no-configure -------------------------------------------------------------
nc = T["spec"].split("#### Scenario: `--no-configure`")[-1]
check("9.22 去掉了「要么…要么…」分支式条款",
      "若确实无法生成" not in nc and "或**以" not in nc and "SHALL 以 **codegen 专属**的错误信息失败" not in nc, "")
# 2026-09-13: the task is implemented now, so the requirement moved from "state that it is red" to
# "state the outcome AND keep the red-before evidence". The criterion follows the task, not vice versa.
check("9.22 单一可核结果 + 实现状态与红态证据并存",
      "确实执行过" in nc and "已通过" in nc and "修前红态" in nc, "")

# --- 9.17 anchors verbatim -----------------------------------------------------------
ANCHORS = [
    ("build.py", "if configure_step:"),
    ("build.py", "patch_ninja()"),
    ("build.py", "codegen()"),
    ("build.py", "build(target)"),
    ("build.py", "len(t) > 2"),
    ("build.py", "_msvc_to_clang_env"),
]
missing = []
for f, a in ANCHORS:
    r = subprocess.run(["git", "grep", "-c", "-F", a, "--", f],
                       cwd=str(ROOT), capture_output=True, text=True)
    if r.returncode != 0:
        missing.append(f"{f} :: {a}")
check("9.17 引文锚点逐字可 git grep -F 命中", not missing,
      "\n".join(f"  MISS {m}" for m in missing) or f"all {len(ANCHORS)} anchors hit")
# any ellipsis-quote used as if verbatim must be marked as paraphrase
ell = [(k, l.strip()) for k, l in (("design", x) for x in T_PRODUCT["design"].splitlines())
       if re.search(r"set \"CC=\.\.\.", l)]
check("9.17 转述必须显式标注（省略号引文不得冒充逐字）",
      all("转述" in l for _, l in ell),
      "\n".join(f"  {k}: {l[:120]}" for k, l in ell) or "(no such quote)")

# --- 9.18 present-tense false claims --------------------------------------------------
def present_tense_false(text: str) -> list[str]:
    out = []
    for line in text.splitlines():
        if "现写代码插在" in line:
            out.append(line)
        if re.search(r"§3\.1 把 `clang_parseTranslationUnit2` 的签名\*\*写反\*\*", line) \
                and "已" not in line:
            out.append(line)
        if re.search(r"正文\*\*仍写\*\*", line):
            out.append(line)
    return out


bad18 = present_tense_false(T_PRODUCT["tasks"])
check("9.18 无「修复后仍说旧话」的现在时假陈述", not bad18,
      "\n".join(f"  {l.strip()[:130]}" for l in bad18) or "(none)")

# --- regression: round-1 criteria must stay green ------------------------------------
r = subprocess.run([sys.executable, str(ROOT / ".review_tmp" / "verify_round1.py")]
                   + (["--root", str(ALT)] if ALT else []),
                   cwd=str(ROOT), capture_output=True, text=True, encoding="utf-8",
                   errors="replace",
                   env={**__import__("os").environ, "PYTHONIOENCODING": "utf-8"})
last = [l for l in r.stdout.splitlines() if l.startswith("TOTAL")]
check("回归：第 14 轮的 14 条判据仍全绿", "0 FAILED" in (last[0] if last else ""),
      last[0] if last else r.stdout[-300:])

print()
failed = [n for n, ok in results if not ok]
print(f"TOTAL {len(results)} checks, {len(failed)} FAILED")
for n in failed:
    print(f"  FAILED: {n}")
sys.exit(1 if failed else 0)
