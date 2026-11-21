#!/usr/bin/env python3
"""产物逐字节基线对账（codegen-native-binding-prereqs 任务 1.1）。

本脚本回答一个问题：**生成器换源（编号从 `CA_BIND(编号)` 标注迁到锁文件）之后，
既有绑定产物是不是一个字节都没变？**

输入有两样：
  · 基线文件：`<sha256>\\t<相对产物目录的路径>` 一行一个，文件头用注释行记录
    「条目数」「git rev-parse HEAD」「git status --porcelain」三样快照；
  · 产物目录：`out/build/<预设>/codegen/bindings`（含 `fixture/` 子目录）。

判定口径（三条，全部非零点名）：
  ① 基线里列出的文件在产物目录里找不到  ⇒ 缺失；
  ② 找到但 SHA-256 与基线不同          ⇒ 内容变了；
  ③ 产物目录里有、基线里没有、也不在「允许新增」清单里 ⇒ 多余。

「允许新增」是本批新增夹具的产物（基线取自本批开工前，新夹具产物本来就不在基线里）；
清单写在基线文件的 `# + <路径>` 行里，逐项登记，不许整类放行。

退出码：
  0 = 全绿
  1 = 有缺失 / 内容不一致 / 未登记的多余文件
  2 = 判据自身失效（基线文件不存在、产物目录不存在、基线「条目数」与实际行数不符）

**当前基线**：`openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/evidence/基线产物哈希.txt`（4.2 首锚；
7.1 因 GPUBuffer 直绑 + ABI v12 又换锚一次，上一锚 200 条封存在同目录 `7.1-换锚前-基线产物哈希.txt`；
4.2 的旧锚 191 条仍在 `openspec/changes/archive/2026-11-09-csharp-native-direct-binding/evidence/基线产物哈希.txt`，
副本在 `evidence/4.2-旧锚封存-基线产物哈希.txt`）。换锚前后的逐文件差异见
`openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/evidence/4.2-换锚前后对照.txt` 与
`evidence/7.1-换锚前后对照.txt`。

`--self-test` 会在内存里删掉基线的一条数据行再算一遍：如果删掉之后判定结果没有变化
（说明脚本根本没在逐行读基线），退出 2 并打印「判据自身失效」。这是防止「脚本写错还永远绿」
的自检，不替代正常对账。
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

HASH_RE = re.compile(r"^[0-9a-fA-F]{64}$")
COUNT_RE = re.compile(r"^#\s*条目数\s+(\d+)\s*$")
ALLOW_PREFIX = "# + "


class Baseline:
    def __init__(self) -> None:
        self.entries: dict[str, str] = {}   # 相对路径（/ 分隔）→ 小写 sha256
        self.declared_count: int | None = None
        self.allow_new: set[str] = set()


def read_baseline(path: Path) -> Baseline:
    baseline = Baseline()
    for raw in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        line = raw.rstrip()
        if not line.strip():
            continue
        if line.startswith(ALLOW_PREFIX):
            baseline.allow_new.add(line[len(ALLOW_PREFIX):].strip().replace("\\", "/"))
            continue
        count_match = COUNT_RE.match(line)
        if count_match:
            baseline.declared_count = int(count_match.group(1))
            continue
        if line.lstrip().startswith("#"):
            continue
        parts = re.split(r"\t| {2,}", line.strip(), maxsplit=1)
        if len(parts) != 2 or not HASH_RE.match(parts[0]):
            raise ValueError(f"基线行格式不对（应为 <sha256>\\t<相对路径>）：{raw!r}")
        digest, rel = parts[0].lower(), parts[1].strip().replace("\\", "/")
        if rel in baseline.entries:
            raise ValueError(f"基线里同一路径出现两次：{rel}")
        baseline.entries[rel] = digest
    return baseline


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def product_files(product_dir: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for path in sorted(product_dir.rglob("*")):
        if path.is_file():
            result[path.relative_to(product_dir).as_posix()] = path
    return result


def compare(baseline: Baseline, product_dir: Path) -> tuple[list[str], list[str], list[str]]:
    """返回（缺失, 内容不一致, 未登记的多余文件）。"""
    files = product_files(product_dir)
    missing: list[str] = []
    changed: list[str] = []
    extra: list[str] = []
    for rel, expected in sorted(baseline.entries.items()):
        path = files.get(rel)
        if path is None:
            missing.append(rel)
            continue
        actual = sha256_of(path)
        if actual != expected:
            changed.append(f"{rel}（基线 {expected[:12]}… / 实际 {actual[:12]}…）")
    for rel in sorted(files):
        if rel not in baseline.entries and rel not in baseline.allow_new:
            extra.append(rel)
    return missing, changed, extra


def print_report(missing: list[str], changed: list[str], extra: list[str]) -> None:
    for rel in missing:
        print(f"  缺失：基线里有、产物目录里没有 —— {rel}", file=sys.stderr)
    for rel in changed:
        print(f"  不一致：内容与基线不同 —— {rel}", file=sys.stderr)
    for rel in extra:
        print(f"  多余：产物目录里有、基线没登记 —— {rel}", file=sys.stderr)
    print(f"基线对账：缺失 {len(missing)} / 不一致 {len(changed)} / 多余 {len(extra)}")


def load_checked(path: Path, product_dir: Path) -> tuple[Baseline, list[str], list[str], list[str]]:
    baseline = read_baseline(path)
    if baseline.declared_count is None:
        raise ValueError("基线文件缺少 `# 条目数 <N>` 头，无法判断基线是否被截断")
    if baseline.declared_count != len(baseline.entries):
        raise ValueError(
            f"基线「条目数 {baseline.declared_count}」与实际数据行 {len(baseline.entries)} 条不符"
            f" —— 基线被改过（删行 / 加行），判据自身失效")
    missing, changed, extra = compare(baseline, product_dir)
    return baseline, missing, changed, extra


def main() -> int:
    parser = argparse.ArgumentParser(description="绑定产物逐字节基线对账")
    parser.add_argument("--baseline", required=True, help="基线文件路径")
    parser.add_argument("--product-dir", required=True, help="产物目录（codegen/bindings）")
    parser.add_argument("--self-test", action="store_true",
                        help="自检：在内存里删掉一条基线行，要求判定结果随之变化")
    args = parser.parse_args()

    baseline_path = Path(args.baseline)
    product_dir = Path(args.product_dir)
    if not product_dir.is_absolute():
        product_dir = (Path.cwd() / product_dir)
    product_dir = product_dir.resolve()
    if not baseline_path.is_file():
        print(f"找不到基线文件：{baseline_path} —— 判据自身失效", file=sys.stderr)
        return 2
    if not product_dir.is_dir():
        print(f"产物目录不存在：{product_dir} —— 判据自身失效", file=sys.stderr)
        return 2
    try:
        baseline, missing, changed, extra = load_checked(baseline_path.resolve(), product_dir)
    except ValueError as error:
        print(f"{error} —— 判据自身失效", file=sys.stderr)
        return 2

    if args.self_test:
        if not baseline.entries:
            print("基线里一条数据行都没有，自检无从做起 —— 判据自身失效", file=sys.stderr)
            return 2
        # 内存里删掉一条数据行（不动磁盘文件）：如果判定结果不变，说明这个脚本没有逐行读基线。
        trimmed = Baseline()
        trimmed.entries = dict(baseline.entries)
        trimmed.declared_count = baseline.declared_count
        trimmed.allow_new = set(baseline.allow_new)
        removed = sorted(trimmed.entries)[0]
        del trimmed.entries[removed]
        # 直接重算（不经过条目数检查：自检模拟的是「删行后仍按原条目数读」的脚本行为）
        t_missing, t_changed, t_extra = compare(trimmed, product_dir)
        # 删掉一条基线行之后，对应文件要么变成「多余」，要么本来就缺失；两种都说明这一次删除被察觉。
        noticed = removed in t_missing or removed in t_extra or any(
            item.startswith(removed) for item in t_extra)
        if not noticed:
            print(f"自检失败：删掉基线行 {removed} 之后判定结果没有变化 —— 判据自身失效",
                  file=sys.stderr)
            return 2
        print(f"自检通过：删掉基线行 {removed} 之后被点名（缺失或多余），脚本确实逐条对账")
        return 0

    print_report(missing, changed, extra)
    return 1 if (missing or changed or extra) else 0


if __name__ == "__main__":
    sys.exit(main())
