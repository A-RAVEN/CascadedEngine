#!/usr/bin/env python3
"""旧序列化 / 哈希设施的收尾判据：一条命令，三个模式，逐条打印读数。

这个脚本是"旧设施是否已经彻底退场"的唯一执行装置。它查三件事：

  1. **符号**（默认模式）：构建目录里的生成物（`out/build/<预设>/codegen/<模块>/`）+ 引擎源码里，
     旧设施被写的每一个符号各命中多少行。
     判定 = 未豁免命中 0 行，**并且**白名单里每一条都真的命中了一行（不留过期的豁免）。
  2. **三份旧头**（`--files-exist`）：用**文件系统枚举**确认
     `CACore/header/Reflection.h` / `Serialization.h` / `Hasher.h` 三个路径不存在。
     改名、`git rm --cached`、搬到新头、移出 include 路径都不算 —— 只认"这个路径还在不在"。
  3. **旧实现指纹**（`--fingerprints`）：旧实现的特征词，防止它换个名字留下来。

扫描范围、排除清单、白名单全部写死在本文件顶部，不看命令行参数、不做"看着办"的推断。

用法：
    python Tools/CodeGen/check_legacy_symbols.py                 # 符号
    python Tools/CodeGen/check_legacy_symbols.py --fingerprints   # 旧实现指纹
    python Tools/CodeGen/check_legacy_symbols.py --files-exist    # 三份旧头
    python Tools/CodeGen/check_legacy_symbols.py --all            # 三个都跑（收尾判据）
    python Tools/CodeGen/check_legacy_symbols.py --list           # 只列出会扫哪些文件

退出码：0 = 全绿；1 = 有未豁免命中 / 有旧头还在 / 白名单过期；2 = 用法或环境错误。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SELF = Path(__file__).resolve()

# ---------------------------------------------------------------------------------------------
# 1. 符号表。写死；每项是（符号, 用于匹配的正则）。
#    `CA_REFLECTION` 那条带否定环视：`CA_REFLECTION_TEMPLATE` 是允许保留的类模板形态
#    （见 openspec 工件里的唯一保留项），它不能被算成 `CA_REFLECTION` 的命中。
# ---------------------------------------------------------------------------------------------
LEGACY_SYMBOLS = [
    ("cacore::serializer", r"cacore::serializer"),
    ("cacore::deserializer", r"cacore::deserializer"),
    ("cacore::batch_deserializer", r"cacore::batch_deserializer"),
    ("cacore::aggregateHasher", r"cacore::aggregateHasher"),
    ("cacore::custom_hash_trait", r"cacore::custom_hash_trait"),
    ("cacore::has_custom_hash_func", r"cacore::has_custom_hash_func"),
    ("careflection::containerStates", r"careflection::containerStates"),
    ("careflection::managed_wrapper_traits", r"careflection::managed_wrapper_traits"),
    ("CATypeDescriptor", r"CATypeDescriptor"),
    ("CA_REFLECTION", r"CA_REFLECTION(?!_TEMPLATE)"),
    ("CA_REFLECTION_TEMPLATE", r"CA_REFLECTION_TEMPLATE"),
]

# ---------------------------------------------------------------------------------------------
# 2. 生成物的扫描目标：**构建目录**（`out/build/<预设>/codegen/<模块>/`）。
#
#    原先是源码树里的三个入库目录（`Tools/CodeGen/generated` 等），本 change 把生成物搬出源码树之后
#    那些目录就没了 —— 脚本会在第一行报 "generated dir missing" 直接退出 2，而另一个 change 的收尾
#    判据就永远拿不到读数。
#
#    换成构建目录之后的**恒真风险变了**，所以判据也换了：不再是"目录被 gitignore、工具静默不生成时
#    它恰好是空的"，而是"**根本没构建过 / 生成失败时目录不存在**"。所以下面把"一个产物目录都找不到"
#    判成**错误（退出码 2）**，不是"0 命中"；跑完还会打印扫到多少个文件，N=0 不可能悄悄变绿。
#    扫描目标由 `out/build/*/codegen` 现场枚举（有几个预设就扫几个），不写死预设名。
# ---------------------------------------------------------------------------------------------
BUILD_ROOT = REPO / "out" / "build"

# 引擎源码的后缀白名单。
SOURCE_SUFFIXES = (".h", ".hpp", ".cpp", ".cxx")

# 目录级排除。每一条都要有理由：
#   openspec/                          —— 工件与归档里的原始证据（历史记录，不是当前实现）
#   out/                               —— 构建产物（含 _deps 拉下来的第三方）
#   .git/                              —— 版本库自身
#   .review_tmp/                       —— .gitignore 里的评审临时目录：早先几轮审查工具写的源码副本，
#                                         构建不读它，也不是引擎源码（保留原地不删，只不参与扫描）
#   Test/VulkanRendererBackendTester/  —— 0 个编译单元（compile_commands.json 里没有），本 change 不迁移
#   CAResource/                        —— 同上，0 个编译单元
#   VulkanRenderBackend/               —— 旧后端，0 个编译单元
EXCLUDED_DIR_PREFIXES = (
    "openspec/",
    "out/",
    ".git/",
    ".review_tmp/",
    "Test/VulkanRendererBackendTester/",
    "CAResource/",
    "VulkanRenderBackend/",
)

# ---------------------------------------------------------------------------------------------
# 3. 白名单：逐行，只允许"必须保留"的行。判定 = 未豁免命中 0 行且每条白名单都命中一行，
#    所以白名单多一条、或者某一条指向的行今天不存在了，都会让脚本变红（不许留过期的豁免）。
#    每条 =（符号, 文件（相对仓库根，正斜杠）, 该行必须包含的子串, 理由）。
# ---------------------------------------------------------------------------------------------
WHITELIST: list[tuple[str, str, str, str]] = []

# ---------------------------------------------------------------------------------------------
# 4. 指纹：旧实现的特征词。
# ---------------------------------------------------------------------------------------------
FINGERPRINTS = [
    ("visit_members", r"visit_members"),
    ("CATypeMemberDesc", r"CATypeMemberDesc"),
    ("containerInfo", r"containerInfo"),
    ("SubmitAndWait", r"SubmitAndWait"),
    ("Object must be trivially copyable", r"Object must be trivially copyable"),
    ("hash_one", r"hash_one"),
    ("managed_pointer_traits", r"managed_pointer_traits"),
]

# 指纹的**文件级**例外。`SubmitAndWait` 同时是 IOManager 自己的批处理接口
# （`Interface/IOManager/header/IOManager/IOManager.h` 的纯虚函数、`IOManager_FS` 的实现、
# `GeneralResources/private/ResourceImportingSystem.cpp` 的调用点），跟旧序列化无关；
# 旧序列化里那几处在 `Serialization.h`，随文件删除一起消失。
# 只列文件、只对指定的指纹生效；除这些文件之外任何地方再出现 `SubmitAndWait` 都算命中。
FINGERPRINT_ALLOWED_FILES = {
    "SubmitAndWait": {
        "GeneralResources/private/ResourceImportingSystem.cpp",
        "IOManager_FS/IOManager.cpp",
        "Interface/IOManager/header/IOManager/IOManager.h",
    },
}

# ---------------------------------------------------------------------------------------------
# 5. 三份旧头。
# ---------------------------------------------------------------------------------------------
LEGACY_HEADERS = [
    "CACore/header/Reflection.h",
    "CACore/header/Serialization.h",
    "CACore/header/Hasher.h",
]


def is_excluded(rel: str) -> bool:
    return any(rel.startswith(p) for p in EXCLUDED_DIR_PREFIXES)


def generated_roots() -> list[Path]:
    """生成物的扫描根：每个构建目录下的 `codegen/`。一个都没有 = 环境错，不是"0 命中"。"""
    roots = sorted(p for p in BUILD_ROOT.glob("*/codegen") if p.is_dir())
    if not roots:
        print(f"ERROR: 找不到任何生成物目录（{BUILD_ROOT}/*/codegen）—— 先跑一次 "
              f"`python build.py --config Debug`，否则这个判据扫的是空集")
        raise SystemExit(2)
    return roots


def iter_scan_files() -> list[Path]:
    """扫描范围：各构建目录下生成物的全部文件 + 引擎源码（按后缀白名单与排除清单）。"""
    files: list[Path] = []
    for root in generated_roots():
        files += sorted(p for p in root.rglob("*") if p.is_file())
    for path in sorted(REPO.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        rel = path.relative_to(REPO).as_posix()
        if is_excluded(rel) or path == SELF:
            continue
        files.append(path)   # 生成物在 out/ 下，已被上面的排除清单挡掉，不会重复计入
    return files


def collect(files: list[Path], patterns: list[tuple[str, str]]) -> dict[str, list[tuple[str, int, str]]]:
    compiled = [(name, re.compile(rx)) for name, rx in patterns]
    hits: dict[str, list[tuple[str, int, str]]] = {name: [] for name, _ in compiled}
    for path in files:
        rel = path.relative_to(REPO).as_posix()
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            print(f"ERROR: cannot read {rel}: {exc}")
            raise SystemExit(2)
        for lineno, line in enumerate(text.splitlines(), 1):
            for name, rx in compiled:
                if rx.search(line):
                    hits[name].append((rel, lineno, line.strip()))
    return hits


def classify(
    hits: dict[str, list[tuple[str, int, str]]],
    patterns: list[tuple[str, str]],
    allowed_files: dict[str, set[str]] | None = None,
    whitelist: list[tuple[str, str, str, str]] | None = None,
) -> tuple[list[str], int, set[int]]:
    """把命中分成"豁免"与"未豁免"。返回（报告行, 未豁免行数, 命中的白名单下标集合）。"""
    allowed_files = allowed_files or {}
    whitelist = whitelist or []
    report: list[str] = []
    total_unwhitelisted = 0
    matched_whitelist: set[int] = set()
    for name, _ in patterns:
        found = hits[name]
        unwhitelisted: list[tuple[str, int, str]] = []
        exempt = 0
        for rel, lineno, text in found:
            if rel in allowed_files.get(name, set()):
                exempt += 1
                continue
            hit_index = next((i for i, (wsym, wrel, wcontains, _) in enumerate(whitelist)
                              if wsym == name and wrel == rel and wcontains in text), None)
            if hit_index is not None:
                matched_whitelist.add(hit_index)
                exempt += 1
                continue
            unwhitelisted.append((rel, lineno, text))
        total_unwhitelisted += len(unwhitelisted)
        report.append(f"  {name:<36} 命中 {len(found)} 行（豁免 {exempt}，未豁免 {len(unwhitelisted)}）")
        for rel, lineno, text in unwhitelisted[:5]:
            report.append(f"      {rel}:{lineno}: {text[:110]}")
        if len(unwhitelisted) > 5:
            report.append(f"      ... 另外 {len(unwhitelisted) - 5} 行")
    report.append(f"  合计命中 {sum(len(v) for v in hits.values())} 行，未豁免 {total_unwhitelisted} 行")
    return report, total_unwhitelisted, matched_whitelist


def run_symbols(files: list[Path]) -> int:
    print("== 一、逐符号扫描（构建目录里的生成物 + 引擎源码）")
    hits = collect(files, LEGACY_SYMBOLS)
    report, unwhitelisted, matched = classify(hits, LEGACY_SYMBOLS, whitelist=WHITELIST)
    for line in report:
        print(line)
    print(f"  白名单 {len(WHITELIST)} 行，命中其中 {len(matched)} 行")
    for sym, rel, contains, reason in WHITELIST:
        print(f"      {sym} | {rel} | {contains} | {reason}")
    stale = [i for i in range(len(WHITELIST)) if i not in matched]
    print(f"扫描 {len(files)} 个文件、命中 {unwhitelisted} 行（未豁免）")
    if unwhitelisted != 0:
        print("结果：红 —— 还有旧符号没迁完")
        return 1
    if stale:
        print(f"结果：红 —— 白名单第 {stale} 条今天没有命中，是过期豁免")
        return 1
    print("结果：绿 —— 未豁免命中 0 行，且白名单每一条都还在命中")
    return 0


def run_fingerprints(files: list[Path]) -> int:
    print("== 三、旧实现指纹（防止旧实现换个名字留下）")
    hits = collect(files, FINGERPRINTS)
    report, unwhitelisted, _ = classify(hits, FINGERPRINTS, allowed_files=FINGERPRINT_ALLOWED_FILES)
    for line in report:
        print(line)
    print(f"  文件级例外：{ {k: sorted(v) for k, v in FINGERPRINT_ALLOWED_FILES.items()} }")
    print(f"扫描 {len(files)} 个文件、命中 {unwhitelisted} 行（未豁免）")
    if unwhitelisted != 0:
        print("结果：红 —— 旧实现的特征词还在")
        return 1
    print("结果：绿 —— 旧实现指纹零命中")
    return 0


def run_files_exist() -> int:
    print("== 二、三份旧头（文件系统枚举，不认改名 / git rm --cached / 搬到新头）")
    present = []
    for rel in LEGACY_HEADERS:
        p = REPO / rel
        exists = p.is_file()
        print(f"  {'存在  ' if exists else '不存在'}  {rel}")
        if exists:
            present.append(rel)
    if present:
        print(f"结果：红 —— {len(present)} 份旧头还在磁盘上")
        return 1
    print("结果：绿 —— 三份旧头都不在磁盘上")
    return 0


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser(description="旧序列化 / 哈希设施的收尾判据")
    ap.add_argument("--fingerprints", action="store_true", help="扫旧实现指纹")
    ap.add_argument("--files-exist", action="store_true", help="用文件系统枚举查三份旧头")
    ap.add_argument("--all", action="store_true", help="三个模式都跑（收尾判据）")
    ap.add_argument("--list", action="store_true", help="只列出会扫哪些文件")
    args = ap.parse_args()

    files = iter_scan_files()
    if args.list:
        print(f"{len(files)} 个文件：")
        for p in files:
            print(f"  {p.relative_to(REPO).as_posix()}")
        return 0

    rc = 0
    if args.all or args.files_exist:
        rc |= run_files_exist()
        print()
    if args.all or args.fingerprints:
        rc |= run_fingerprints(files)
        print()
    if args.all or not (args.fingerprints or args.files_exist):
        rc |= run_symbols(files)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
