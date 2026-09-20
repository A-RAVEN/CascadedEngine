"""Round 18 (goal 2, round 1/3) criteria for tasks 9.6 and 9.7.

Each check states its 作用对象 / 属性 / 红态 (CLAUDE.md 判据三件套).

Usage:
  python .review_tmp/verify_round18.py --root .review_tmp/r1pre   # RED (pre-fix)
  python .review_tmp/verify_round18.py                            # GREEN (worktree)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALT = None
if "--root" in sys.argv:
    ALT = (ROOT / sys.argv[sys.argv.index("--root") + 1]).resolve()

REL = {
    "tasks": "openspec/changes/engine-codegen-framework/tasks.md",
    "design": "openspec/changes/engine-codegen-framework/design.md",
    "spec": "openspec/changes/engine-codegen-framework/specs/codegen-framework/spec.md",
}


def read(k: str) -> str:
    if ALT is not None:
        p = ALT / Path(REL[k]).name
        return p.read_text(encoding="utf-8") if p.exists() else ""
    return (ROOT / REL[k]).read_text(encoding="utf-8")


T = {k: read(k) for k in REL}
results = []


def check(name: str, ok: bool, evidence: str = "") -> None:
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    for line in evidence.strip().splitlines()[:8]:
        print(f"        {line}")


def block(text: str, start: str, end: str) -> str:
    """Locate by title, not by checkbox state (boxes get ticked; the title does not)."""
    i = text.index(start)
    j = text.index(end, i + len(start))
    return text[i:j]


sec76 = block(T["tasks"], "9.7 **[AUDIT] Round-4 机制叙述事实性更正**", "9.8 **[AUDIT] spec 抽取范围收窄")


# ---- 9.6 : the three remaining sites ------------------------------------------------
# Property, not substring: a rejected wording may still appear **inside a correction**
# (it has to, to say what was wrong). The criterion is therefore "the old claim is not
# *asserted*" — any line carrying it must also carry a correction marker.
FIX_MARK = ("更正", "原文写", "从未做过", "该句为假", "与产物不符")


def asserted(text: str, needle: str) -> list[str]:
    return [l.strip() for l in text.splitlines() if needle in l
            and not any(m in l for m in FIX_MARK)]


sec62 = block(T["tasks"], "- [x] 6.2 ", "\n- [x] 6b.1") if "- [x] 6.2 " in T["tasks"] else ""
bad1 = asserted(sec62, "`operator==`")
check("9.6① §6.2 不再**断言**生成 `operator==`（引用旧措辞处须带更正标注），且点名真实 API",
      not bad1 and re.search(r"equal\(Obj const&, Obj const&\)", sec62) is not None,
      "\n".join(f"  OFF {l[:110]}" for l in bad1) or "无未标注断言"
      + (" | 真实 API 已点名" if "equal(Obj const&, Obj const&)" in sec62 else " | ⚠ 未点名真实 API"))

bad2 = asserted(sec62, "仍哈希相等")
check("9.6② §6.2 不再**断言**「布局不同仍哈希相等」为已做实证（引用处须带更正标注）",
      not bad2 and re.search(r"(论证|从未做过|未实证)", sec62) is not None,
      "\n".join(f"  OFF {l[:110]}" for l in bad2) or "无未标注断言")

sec81 = block(T["tasks"], "- [x] 8.1 ", "## 9. 返工任务")
bad3 = asserted(sec81, "7 条真实缺陷[AUDIT-1..7]") + asserted(sec81, "全部修正合回")
check("9.6③ §8.1 合计状态行逐条如实（旧句若引用须带更正标注，且须逐条列 AUDIT 状态）",
      not bad3 and all(a in sec81 for a in ("AUDIT-2", "AUDIT-4", "AUDIT-5", "AUDIT-6", "AUDIT-3")),
      ("\n".join(f"  OFF {l[:110]}" for l in bad3) or "无未标注的旧句")
      + f" | AUDIT 逐条: {[a for a in ('AUDIT-1','AUDIT-2','AUDIT-3','AUDIT-4','AUDIT-5','AUDIT-6','AUDIT-7') if a in sec81]}")

# ---- 9.7 : exit code must not be used as fidelity evidence --------------------------
for k in ("tasks", "design"):
    hit = re.search(r"保真度?不能靠退出码|不能靠退出码判定", T[k])
    check(f"9.7 {k}: 写明「保真度不能靠退出码判定」", hit is not None, "")

r45 = block(T["tasks"], "### Round 4", "### Round 5") if "### Round 4" in T["tasks"] else ""
check("9.7 Round 4 段：三条机制断言均已带实测更正标注",
      r45.count("2026-09-12") >= 2 and "归因已作废" in r45,
      f"2026-09-12 出现 {r45.count('2026-09-12')} 次；'归因已作废' {'在' if '归因已作废' in r45 else '不在'}")

sec76 = block(T["tasks"], "9.7 **[AUDIT] Round-4 机制叙述事实性更正**", "9.8 **[AUDIT] spec 抽取范围收窄")
check("9.7④「绝不喂全 union」规则仍被保留（不得被一并删掉）",
      "绝不喂全 union" in sec76, "")

print()
failed = [n for n, ok in results if not ok]
print(f"TOTAL {len(results)} checks, {len(failed)} FAILED")
for n in failed:
    print(f"  FAILED: {n}")
sys.exit(1 if failed else 0)
