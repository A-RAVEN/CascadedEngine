#!/usr/bin/env python3
"""check_legacy_symbols.py —— 这个 change 的收尾判据：旧序列化 / 哈希设施在产物里为零。

它把三条判据固化成一条可跑的命令（对应 tasks.md 的 3.4 与 9.4）：

  ① 逐符号扫描：引擎源码（后缀白名单）+ **构建目录里的生成物**（`out/build/*/codegen/**`），
     命中就逐条打印 `文件:行号: 原文`；
  ② 旧实现指纹扫描：旧运行时自己那几句实现特征，命中同样逐条打印；
  ③ 文件系统枚举：`Reflection.h` / `Serialization.h` / `Hasher.h` 三份旧头必须**不存在**
     （不是 `git rm --cached`、不是改名、不是搬进新头）。

用法（默认三条一起跑）：
    python Tools/CodeGen/check_legacy_symbols.py
    python Tools/CodeGen/check_legacy_symbols.py --symbols         # 只跑 ①
    python Tools/CodeGen/check_legacy_symbols.py --fingerprints    # 只跑 ②
    python Tools/CodeGen/check_legacy_symbols.py --files-exist     # 只跑 ③

判据（退出码）：
    * 扫描成功且**每一条判据都为零** → 打印 `扫描 N 个文件、命中 0 行`，退出码 0；
    * 有任何命中没有落在白名单里 → 逐条打印后退出码 1；
    * 用法错 / 仓库根找不到 → 退出码 2。

白名单（`ABI_GUARD_WHITELIST`）：每条 = （仓库相对路径，该行去掉首尾空白后的原文）。
它的**终点状态是空表**（tasks 9.4 的修正：原本打算给消费方 .cpp 里的 ABI 守卫留逐行白名单，
但那个宏与 `CATypeDescriptor` 都定义在旧头里，头一删就编不过，所以改成 0 条）。
机制留着、条目为空 —— 需要临时放行时，加一条并把原因写在下面，不要让判据静默通过。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# 输出里有中文，而重定向到文件时 Python 默认按系统代码页编码（实测 cp1252 → UnicodeEncodeError），
# 所以这里把两个流钉成 UTF-8：日志重定向之后还能读。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# ============================================================================================
# 被判的符号：旧运行时对外露出的名字（限定名写法，不用裸名 —— 裸名会撞上新运行时的同义名字）
# ============================================================================================
LEGACY_SYMBOLS: tuple[str, ...] = (
    "cacore::serializer",
    "cacore::deserializer",
    "cacore::batch_deserializer",
    "cacore::aggregateHasher",
    "cacore::custom_hash_trait",
    "cacore::has_custom_hash_func",
    "careflection::containerStates",
    "careflection::managed_wrapper_traits",
    "CATypeDescriptor",
    "CA_REFLECTION",
)

# ============================================================================================
# 旧实现指纹：不是对外名字，而是旧运行时实现里那几句特征话。旧头删掉之后它们必须一起消失
#
# ⚠ 这里把 tasks.md 原稿里那条 `SubmitAndWait` 换成了 `Submit Count:`（2026-10-05 实测后改的，
#   改前/改后与依据都记在 tasks.md 的 10.3 条目里）：`SubmitAndWait` 是**接口自己的方法名**
#   （`Interface/IOManager/header/IOManager/IOManager.h` 里 `virtual void SubmitAndWait() = 0;`），
#   它随 `ca_io::WBatch` / `IOBatch` 一起留着；实测当时 17 处命中里 11 处是这份仍然在用的接口
#   与它的实现（另有 6 处在待删的 `Serialization.h` 里），拿它当"旧实现指纹"永远清不到 0。
#   换成旧序列化器自己那句调试打印 `Submit Count:` —— 实测全仓只出现在
#   `CACore/header/Serialization.h` 的两处 `finalize()` 里，判别力更强（方法名可以合法复用，
#   这句打印不会）。
# ============================================================================================
LEGACY_FINGERPRINTS: tuple[str, ...] = (
    "visit_members",
    "CATypeMemberDesc",
    "containerInfo",
    "Submit Count:",
    "Object must be trivially copyable",
    "hash_one",
    "managed_pointer_traits",
)

# 三份必须不存在的旧头（相对仓库根）
LEGACY_HEADERS: tuple[str, ...] = (
    "CACore/header/Reflection.h",
    "CACore/header/Serialization.h",
    "CACore/header/Hasher.h",
)

# ============================================================================================
# 扫描范围
# ============================================================================================
SOURCE_SUFFIXES: frozenset[str] = frozenset(
    {".h", ".hpp", ".hh", ".hxx", ".inl", ".cpp", ".cxx", ".cc", ".c", ".py"}
)
# 这些目录整棵跳过：change 工件（里面必然在讨论旧符号）、构建产物（生成物单独扫）、
# 外部依赖、审查中间目录、脚本自己
EXCLUDED_DIRS: frozenset[str] = frozenset(
    {"openspec", "out", ".git", "_deps", ".review_tmp", ".claude", ".vs", "build", "__pycache__"}
)
# 生成物单独扫这一片（"构建目录里的生成物"）——它是编译真正吃进去的那份产物
GENERATED_GLOB = "out/build/*/codegen/**/*.h"

# 路径里带这些片段的一律跳过（生成物那份 glob 之外，避免把别人的中间文件算进来）
EXCLUDED_PATH_PARTS: tuple[str, ...] = ("/_deps/", "/CMakeFiles/", "/.git/")

# ============================================================================================
# 白名单：终点是空表。每条写清"为什么现在放行、什么时候该删"。
# ============================================================================================
ABI_GUARD_WHITELIST: tuple[tuple[str, str], ...] = ()


def repository_root() -> Path:
    """脚本自己的位置推仓库根：`<root>/Tools/CodeGen/check_legacy_symbols.py`。"""
    return Path(__file__).resolve().parents[2]


def iter_scanned_files(root: Path) -> list[Path]:
    """引擎源码（后缀白名单、跳过排除目录）+ 构建目录里的生成物。"""
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        parts = path.relative_to(root).parts
        if any(part in EXCLUDED_DIRS for part in parts[:-1]):
            continue
        posix = "/" + path.relative_to(root).as_posix()
        if any(chunk in posix for chunk in EXCLUDED_PATH_PARTS):
            continue
        if path.resolve() == Path(__file__).resolve():
            continue
        files.append(path)
    for path in root.glob(GENERATED_GLOB):
        if path.is_file():
            files.append(path)
    return sorted(set(files))


def scan(root: Path, needles: tuple[str, ...], files: list[Path]) -> list[tuple[str, int, str, str]]:
    """返回 (仓库相对路径, 行号, 原文, 命中的那个词)。"""
    hits: list[tuple[str, int, str, str]] = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        relative = path.relative_to(root).as_posix()
        for number, line in enumerate(text.splitlines(), start=1):
            for needle in needles:
                if needle in line:
                    hits.append((relative, number, line.strip(), needle))
                    break
    return hits


def whitelisted(relative: str, line: str) -> bool:
    stripped = line.strip()
    return any(entry[0] == relative and entry[1] == stripped for entry in ABI_GUARD_WHITELIST)


def report(title: str, hits: list[tuple[str, int, str, str]], files_scanned: int) -> int:
    """打印一类判据的结果，返回没被白名单放行的命中数。"""
    waived = [hit for hit in hits if whitelisted(hit[0], hit[2])]
    effective = [hit for hit in hits if not whitelisted(hit[0], hit[2])]
    print(f"== {title} ==")
    print(f"扫描 {files_scanned} 个文件、命中 {len(effective)} 行"
          + (f"（另有 {len(waived)} 行在白名单里）" if waived else ""))
    for relative, number, line, needle in effective:
        shown = line if len(line) <= 160 else line[:157] + "..."
        print(f"  {relative}:{number}: [{needle}] {shown}")
    return len(effective)


def check_files_exist(root: Path) -> int:
    print("== 三份旧头必须不存在 ==")
    missing = 0
    for relative in LEGACY_HEADERS:
        exists = (root / relative).exists()
        print(f"  {relative}: {'仍然存在' if exists else '不存在'}")
        missing += 1 if exists else 0
    return missing


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="旧序列化 / 哈希设施的收尾判据")
    parser.add_argument("--symbols", action="store_true", help="只跑逐符号扫描")
    parser.add_argument("--fingerprints", action="store_true", help="只跑旧实现指纹扫描")
    parser.add_argument("--files-exist", action="store_true", help="只跑三份旧头的文件系统枚举")
    args = parser.parse_args(argv)

    root = repository_root()
    if not (root / "Tools" / "CodeGen").is_dir():
        print(f"仓库根不像是对的：{root}", file=sys.stderr)
        return 2

    only_specific = args.symbols or args.fingerprints or args.files_exist
    run_symbols = args.symbols or not only_specific
    run_fingerprints = args.fingerprints or not only_specific
    run_files_exist = args.files_exist or not only_specific

    files = iter_scanned_files(root)
    total = 0
    if run_symbols:
        total += report("逐符号扫描（源码 + 构建目录里的生成物）", scan(root, LEGACY_SYMBOLS, files), len(files))
    if run_fingerprints:
        total += report("旧实现指纹", scan(root, LEGACY_FINGERPRINTS, files), len(files))
    if run_files_exist:
        total += check_files_exist(root)

    print()
    if total == 0:
        print(f"扫描 {len(files)} 个文件、命中 0 行")
        return 0
    print(f"命中 {total} 行（白名单 {len(ABI_GUARD_WHITELIST)} 条）")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
