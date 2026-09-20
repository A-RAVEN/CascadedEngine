"""Round-3 criteria + built-in red probe.

Green:  python .review_tmp/verify_round3.py
Red:    python .review_tmp/verify_round3.py --red      (strips this round's additions
        into .review_tmp/r3red/ and re-runs the same criteria there)

Criteria are written as 作用对象 + 属性 + 红态 (see CLAUDE.md 判据三件套).
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CDIR = ROOT / "openspec" / "changes" / "engine-codegen-framework"
FILES = {
    "tasks": CDIR / "tasks.md",
    "design": CDIR / "design.md",
    "proposal": CDIR / "proposal.md",
    "spec": CDIR / "specs" / "codegen-framework" / "spec.md",
    "claude": ROOT / "CLAUDE.md",
}
RED_DIR = ROOT / ".review_tmp" / "r3red"

# Markers this round added; the red probe removes exactly these lines.
ADDED = ("实际所用工具：无落盘证据", "无落盘测量记录", "判据必须交出「三件套」",
         "改用轮界逐文件哈希", "本行三个 URL 的实际所用工具无落盘证据",
         "本行两个 URL 的实际所用工具无落盘证据",
         "逐 URL 的工具标注（2026-09-12 补")

if "--red" in sys.argv:
    RED_DIR.mkdir(parents=True, exist_ok=True)
    for k, p in FILES.items():
        text = p.read_text(encoding="utf-8")
        kept = [l for l in text.splitlines() if not any(m in l for m in ADDED)]
        (RED_DIR / (k + ".md")).write_text("\n".join(kept), encoding="utf-8")
    print(f"### RED PROBE: {RED_DIR} (this round's additions stripped)\n")
    BASE = RED_DIR
    def rd(k): return (BASE / (k + ".md")).read_text(encoding="utf-8")
else:
    def rd(k): return FILES[k].read_text(encoding="utf-8")


T = {k: rd(k) for k in FILES}
results = []


def check(name: str, ok: bool, evidence: str = "") -> None:
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    for line in evidence.strip().splitlines()[:10]:
        print(f"        {line}")


# --- 9.26① : every URL line carries an actual-tool annotation ------------------------
def url_lines(text: str) -> list[str]:
    return [l for l in text.splitlines() if "http://" in l or "https://" in l]


URL_FLOOR = {"tasks": 8, "design": 1, "proposal": 1, "spec": 0}
for key in ("tasks", "design", "proposal", "spec"):
    lines = url_lines(T[key])
    bad = [l.strip() for l in lines
           if not any(m in l for m in ("harness", "pwsh", "web_fetch", "webReader",
                                       "web_search_prime", "无落盘", "复查"))]
    # companion guard: the criterion must not be satisfiable by *deleting* the URL line
    check(f"9.26① {key}: URL 行数不少于基线（防「删行即通过」）",
          len(lines) >= URL_FLOOR[key], f"url_lines={len(lines)} floor={URL_FLOOR[key]}")
    check(f"9.26① {key}: 每个 URL 行都带工具标注或无落盘声明", not bad,
          f"url_lines={len(lines)} unannotated={len(bad)}\n"
          + "\n".join(f"  OFF {l[:120]}" for l in bad)
          + "\n" + "\n".join(f"  ok  {l.strip()[:90]}" for l in lines if l.strip() not in bad))

# --- 9.26② : the "Round 6 had no MCP tool" claim must be marked as self-reported -----
def strip_task_blocks(text: str) -> str:
    """§9.11 / §9.26 enumerate the sites they want fixed and therefore *quote* the claim.
    A criterion must judge the claim sites, not the paragraph that lists them.

    NOTE: the Review Log is deliberately NOT stripped here — the MCP claim sites themselves live in
    the round records, so stripping it made the criterion unfalsifiable ("claim site not found").
    Lines that *record the fix* carry their own markers and pass on their own merits.
    """
    for marker in ("- [ ] 9.11 ", "- [ ] 9.26 ", "- [x] 9.11 ", "- [x] 9.26 "):
        while marker in text:
            i = text.index(marker)
            j = text.find("\n- [", i + 1)
            text = text[:i] + (text[j:] if j != -1 else "")
    return text


claim = [l for l in strip_task_blocks(T["tasks"]).splitlines() if "均无 MCP web 工具" in l]
check("9.26② 「Round 6 无 MCP 工具」已标注为无落盘测量记录",
      bool(claim) and all("无落盘测量记录" in l or "自述" in l for l in claim),
      "\n".join(f"  {l.strip()[:150]}" for l in claim) or "(claim site not found)")

# --- 9.24 : the criteria-discipline rules landed in CLAUDE.md ------------------------
cl = T["claude"]
check("9.24 判据三件套（作用对象/属性/红态）已写进 CLAUDE.md",
      "判据必须交出「三件套」" in cl and "作用对象" in cl and "不是字符串命中" in cl, "")
check("9.24 锚点自检必须限定目标文件（禁止全仓 grep）已写进 CLAUDE.md",
      "必须加 `-- <目标文件>`" in cl, "")
check("9.24 空转轮判据改为轮界逐文件哈希（原 git diff --name-only 被判不可靠）",
      "改用轮界逐文件哈希" in cl and "恒非空" in cl, "")

# --- regression: rounds 1 and 2 criteria stay green ---------------------------------
for script in ("verify_round1.py", "verify_round2.py"):
    r = subprocess.run([sys.executable, str(ROOT / ".review_tmp" / script)],
                       cwd=str(ROOT), capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env={**__import__("os").environ, "PYTHONIOENCODING": "utf-8"})
    total = [l for l in (r.stdout or "").splitlines() if l.startswith("TOTAL")]
    check(f"回归：{script} 仍全绿", "0 FAILED" in (total[0] if total else ""),
          total[0] if total else (r.stdout or "")[-200:])

print()
failed = [n for n, ok in results if not ok]
print(f"TOTAL {len(results)} checks, {len(failed)} FAILED")
for n in failed:
    print(f"  FAILED: {n}")
sys.exit(1 if failed else 0)
