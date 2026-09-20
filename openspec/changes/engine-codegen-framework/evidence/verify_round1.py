"""Round-1 verification: executable criteria for the artifact corrections.

Every check prints PASS/FAIL plus the raw evidence lines, so the criterion's
ability to fail can be inspected (red-green), not just asserted.

Usage: python .review_tmp/verify_round1.py
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# --root <dir> lets the same criteria run against a pre-fix snapshot (copies of the four
# artifacts), which is how the red-green evidence for these criteria is produced.
ALT = None
if "--root" in sys.argv:
    ALT = Path(sys.argv[sys.argv.index("--root") + 1]).resolve()
    if not ALT.is_absolute():
        ALT = (ROOT / ALT).resolve()


def _read(rel: str) -> str:
    if ALT is not None:
        p = ALT / Path(rel).name
        return p.read_text(encoding="utf-8") if p.exists() else ""
    return (ROOT / rel).read_text(encoding="utf-8")


CDIR_REL = "openspec/changes/engine-codegen-framework"
TASKS = _read(f"{CDIR_REL}/tasks.md")
DESIGN = _read(f"{CDIR_REL}/design.md")
SPEC = _read(f"{CDIR_REL}/specs/codegen-framework/spec.md")
PROPOSAL = _read(f"{CDIR_REL}/proposal.md")
if ALT is not None:
    print(f"### RUNNING AGAINST SNAPSHOT: {ALT} (red-state probe)\n")

results = []


def check(name: str, ok: bool, evidence: str) -> None:
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    for line in evidence.strip().splitlines():
        print(f"        {line}")


def lines_with(text: str, needle: str) -> list[str]:
    return [l.strip() for l in text.splitlines() if needle in l]


# 1. build.py line-number citations must be gone (except the two documented exceptions:
#    §9.11① transcribing the erroneous text it corrects, and Round 13's B-table quoting it).
print("=" * 72)
print("1. build.py 行号引用（规约禁止；例外只允许「转述被纠正的对象」）")
print("=" * 72)
hits = []
for path, text in (("tasks.md", TASKS), ("design.md", DESIGN), ("proposal.md", PROPOSAL), ("spec.md", SPEC)):
    src_lines = text.splitlines()
    for idx, line in enumerate(src_lines, 1):
        if re.search(r"build\.py:\d+", line):
            hits.append((path, idx, line.strip()))
# Exemptions allowed by the §9 引用规约: transcribing the erroneous text that is being
# corrected (①), and historical records of what round N left unfixed.
EXEMPT = ("被纠正的对象", "仍写", "原写", "未修：9.11", "引文锚点")
live = [h for h in hits if not any(mark in h[2] for mark in EXEMPT)]
check("no live build.py:<line> citation", not live,
      f"total={len(hits)} live={len(live)}\n"
      + "\n".join(f"  live {p}:{n}: {l[:130]}" for p, n, l in live)
      + "\n" + "\n".join(f"  exempt {p}:{n}: {l[:110]}" for p, n, l in hits if (p, n, l) not in live))

# 2. The reversed signature claim must be gone (only inside its own correction).
print()
print("=" * 72)
print("2. clang_parseTranslationUnit2 签名（不得再声称「返回 TU、错误码在 out-param」）")
print("=" * 72)
bad = [l for l in lines_with(TASKS, "真实签名返回 CXTranslationUnit") if "写反" not in l]
check("no un-corrected reversed signature claim", not bad, "\n".join(bad) or "(none)")

# 3. MCP declarations: no bare "MCP 核..." assertion left.
print()
print("=" * 72)
print("3. MCP 工具声明（8 处须改毕；不得再有无保留的「MCP 核到」）")
print("=" * 72)
bare = [l.strip() for l in lines_with(TASKS, "MCP 核")
        if not any(m in l for m in ("原写", "无落盘证据可核", "属前瞻性表述", "同 Round", "§8.1", "属 8.1 正式 review", "9.21"))]
check("no unqualified 'MCP 核' claim", not bare, "\n".join(bare) or "(none)")
sites = {
    "① 文首": "各轮实际是否具备该工具，须逐轮如实标注" in TASKS,
    "② §8.1 任务行": any("外部 API 用 MCP 核查" in l and "无落盘证据可核" in l for l in TASKS.splitlines()),
    "④ Round5 范围行": "只陈述**可核事实**" in TASKS,
    "⑤ Round5 核查结论": "MCP 核到真实 URL" in TASKS and "无落盘证据可核**，见上" in TASKS,
    "⑥ Round5 表格行": "无落盘证据可核**；该引文与其 URL 已由 Round 6 独立复查命中" in TASKS,
    "⑧ Round6 正文": "已由 §9.11④ 处置" in TASKS,
}
check("all enumerated MCP sites annotated", all(sites.values()),
      "\n".join(f"{k}: {'ok' if v else 'MISSING'}" for k, v in sites.items()))

# 4. spec.md normative fixes.
print()
print("=" * 72)
print("4. spec.md 的规范性修正")
print("=" * 72)
check("REQ#build.py 不再写行号", "build.py:230" not in SPEC and "configure 步骤内" in SPEC, "")
check("--no-configure 分支已补", "--no-configure` 的增量构建也须走 codegen" in SPEC, "")
check("漂移方向已标注为不在本 change 范围", "不在本 change 实现范围" in SPEC
      and "与注解漂移（字段增删）→" not in SPEC, "")
check("「（如实现支持）」只剩被引用的删除记录", all("已删除" in l or "不可能失败" in l
      for l in lines_with(SPEC, "（如实现支持）")),
      "\n".join(lines_with(SPEC, "（如实现支持）")) or "(none)")
check("抽取范围已收窄为字段-only", "本 change 只提取数据字段" in SPEC, "")
check("两条通道按可见性划分已写入", "两条标注通道按成员可见性划分" in SPEC, "")

# 5. /MDd limitation landed at the two required sites.
print()
print("=" * 72)
print("5. /MDd 配置局限（须落 §4.3 正文 + design 迁移计划，且排除 §8.1/§9 自身）")
print("=" * 72)
sec43 = TASKS.split("## 4. build.py 接入 codegen 步骤")[1].split("## 5. ")[0]
mig = DESIGN.split("## Migration Plan")[1]
check("§4.3 段内命中", "/MDd" in sec43 and "_Container_proxy" in sec43,
      f"/MDd={sec43.count('/MDd')} _Container_proxy={sec43.count('_Container_proxy')}")
check("design 迁移计划命中", "/MDd" in mig and "_Container_proxy" in mig,
      f"/MDd={mig.count('/MDd')} _Container_proxy={mig.count('_Container_proxy')}")

# 6. Structure still intact.
print()
print("=" * 72)
print("6. 结构完整性")
print("=" * 72)
h2 = [l for l in TASKS.splitlines() if l.startswith("## ")]
check("Review Log 之后无其它二级标题", h2[-1].startswith("## Review Log"), f"last h2 = {h2[-1]}")
fences = [l for l in TASKS.splitlines() if l.strip().startswith("```")]
check("代码围栏成对", len(fences) % 2 == 0, f"fence markers = {len(fences)}")

print()
print("=" * 72)
failed = [n for n, ok in results if not ok]
print(f"TOTAL {len(results)} checks, {len(failed)} FAILED")
for n in failed:
    print(f"  FAILED: {n}")
print("=" * 72)
sys.exit(1 if failed else 0)
