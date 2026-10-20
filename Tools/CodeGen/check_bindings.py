#!/usr/bin/env python3
"""对拍：绑定生成的 C ABI 签名，是否逐行等于 BindableShapes.expected.txt 里写的。

期望文件每行前缀矩阵行号（`01|` … `14|`），随后是期望的 C ABI 签名（逐字）。
生成物在 out/build/<预设>/codegen/bindings/<接口>.Bindings.cpp。

两件事分开看：
  · 行 11（`拒绝…`）与行 14（`错误码 + 文本…`）不是签名：它们分别由 2.6 的拒绝读数与 3.2 的端到端覆盖，
    这里只统计"有几行是签名"，不参与逐字比对。
  · 生成物还不存在时，脚本打"缺产物"并退出 1 —— 那是生成器未落地时的红态。

退出码：
  0 = 所有签名逐行对上
  1 = 有对不上的地方（逐条打印行号、期望、实际），或产物缺失
  2 = 判据自身失效（找不到期望文件 / 夹具头）
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
EXPECTED_FILE = HERE / "BindableShapes.expected.txt"
FIXTURE_HEADER = HERE / "BindableShapes.h"
PRODUCT_DIR = REPO / "out" / "build" / "x64-debug" / "codegen" / "bindings"

ROW_RE = re.compile(r"^(\d\d)\|(.*)$")
SYMBOL_RE = re.compile(r"\b(IBindRow\d\d_\w+)\s*\(")
DECL_RE = re.compile(r"extern\s+\"C\"[^;{]*?;", re.S)


def read_expected() -> tuple[list[tuple[str, str]], int, int]:
    """返回（签名行列表 [(行号, 期望签名)], 非签名行数, 出现过的行号数）。"""
    if not EXPECTED_FILE.is_file():
        raise FileNotFoundError(str(EXPECTED_FILE))
    signatures: list[tuple[str, str]] = []
    special = 0
    rows_seen: set[str] = set()
    for line in EXPECTED_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        match = ROW_RE.match(line.strip())
        if not match:
            continue
        row, body = match.group(1), match.group(2).strip()
        rows_seen.add(row)
        if body.startswith("拒绝") or body.startswith("错误码"):
            special += 1
            continue
        signatures.append((row, body))
    return signatures, special, len(rows_seen)


def normalize(text: str) -> str:
    """把签名压成可比形态：去掉多余空白、去掉参数名、去掉末尾分号。"""
    text = re.sub(r"\s+", " ", text).strip().rstrip(";")
    if text.startswith('extern "C"'):
        text = text[len('extern "C"'):].strip()
    # 去掉参数名（`int32_t value` → `int32_t`，`void* self` → `void*`）；只处理逗号分隔的形参
    head, _, args = text.partition("(")
    if not args:
        return text
    args = args.rstrip(")")
    depth = 0
    parts = []
    current = ""
    for character in args:
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
        if character == "," and depth == 0:
            parts.append(current)
            current = ""
            continue
        current += character
    parts.append(current)
    cleaned = []
    for part in parts:
        part = part.strip()
        if not part:
            cleaned.append(part)
            continue
        tokens = part.split(" ")
        if len(tokens) >= 2 and not tokens[-1].endswith("*"):
            part = " ".join(tokens[:-1])
        cleaned.append(part)
    return head.strip() + "(" + ", ".join(cleaned) + ")"


def read_generated() -> dict[str, str]:
    """返回 {符号名: 生成出来的声明（原文）}。"""
    found: dict[str, str] = {}
    if not PRODUCT_DIR.is_dir():
        return found
    for path in sorted(PRODUCT_DIR.glob("*.Bindings.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for declaration in DECL_RE.findall(text):
            match = SYMBOL_RE.search(declaration)
            if match:
                found[match.group(1)] = declaration
    return found


CPP_SYMBOL_RE = re.compile(r'extern\s+"C"\s+[^;{]*?\b(\w+)_(\w+)\s*\([^;{]*;', re.S)
CS_MEMBER_RE = re.compile(r"public static delegate\* unmanaged<[^>]*>\s*(\w+)\s*;")
COMPANION_SUFFIXES = ("Len", "Count", "GetApi")
COMPANION_PREFIXES = ("Copy", "Release", "LastError")


def is_companion(name: str) -> bool:
    """伴生函数（两段式 / 释放 / 错误通道）不在"方法集合"里比 —— 它们不是接口方法。"""
    return name.startswith(COMPANION_PREFIXES) or name.endswith(COMPANION_SUFFIXES)


def check_alignment() -> list[str]:
    """C++ 侧每个接口的方法名与顺序，必须与 C# 包装类里的一致（任务 1.3）。"""
    problems: list[str] = []
    if not PRODUCT_DIR.is_dir():
        return ["两侧都缺：产物目录不存在，C++ 与 C# 都没有可对的东西"]

    cpp_pairs: dict[str, list[str]] = {}
    cs_pairs: dict[str, list[str]] = {}
    for path in sorted(PRODUCT_DIR.glob("*.Bindings.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for interface, method in CPP_SYMBOL_RE.findall(text):
            if is_companion(method):
                continue
            cpp_pairs.setdefault(interface, []).append(method)
    for path in sorted(PRODUCT_DIR.glob("*.Bindings.cs")):
        interface = path.name[: -len(".Bindings.cs")]
        text = path.read_text(encoding="utf-8", errors="replace")
        for method in CS_MEMBER_RE.findall(text):
            if is_companion(method):
                continue
            cs_pairs.setdefault(interface, []).append(method)

    for interface in sorted(set(cpp_pairs) | set(cs_pairs)):
        cpp_list = cpp_pairs.get(interface, [])
        cs_list = cs_pairs.get(interface, [])
        if cpp_list == cs_list:
            continue
        only_cpp = [name for name in cpp_list if name not in cs_list]
        only_cs = [name for name in cs_list if name not in cpp_list]
        if only_cpp or only_cs:
            problems.append(f"  {interface}：C++ 多 {only_cpp or '无'}，C# 多 {only_cs or '无'}")
        else:
            problems.append(f"  {interface}：方法集合相同但顺序不同 —— C++ {cpp_list} / C# {cs_list}")
    return problems


def main() -> int:
    if not FIXTURE_HEADER.is_file():
        print(f"找不到夹具头：{FIXTURE_HEADER} —— 判据自身失效", file=sys.stderr)
        return 2
    try:
        signatures, special, row_count = read_expected()
    except FileNotFoundError as exc:
        print(f"找不到期望文件：{exc} —— 判据自身失效", file=sys.stderr)
        return 2

    rows = sorted({row for row, _ in signatures})
    generated = read_generated()

    print(f"矩阵行数 14 / 有夹具的行数 {row_count} / 签名行 {len(signatures)} / "
          f"非签名行（拒绝或错误码）{special}")

    if not generated:
        print(f"缺产物：{PRODUCT_DIR} 下没有 *.Bindings.cpp —— 生成器还没落地（红态）", file=sys.stderr)
        # 两侧对齐在这种状态下也要有个明确结论，不能因为提前返回就不说话（任务 1.3 的红一）
        for problem in check_alignment():
            print(problem, file=sys.stderr)
        return 1

    mismatches = 0
    for row, expected in signatures:
        symbol_match = SYMBOL_RE.search(expected)
        symbol = symbol_match.group(1) if symbol_match else ""
        actual = generated.get(symbol)
        if actual is None:
            print(f"  第 {row} 行：产物里没有 {symbol}", file=sys.stderr)
            mismatches += 1
            continue
        if normalize(actual) != normalize(expected):
            print(f"  第 {row} 行不符：\n    期望 {normalize(expected)}\n    实际 {normalize(actual)}",
                  file=sys.stderr)
            mismatches += 1

    print(f"不符数 {mismatches}")

    # ---- 两侧对齐（任务 1.3）：C++ 侧的方法名与顺序 vs C# 包装里的方法名与顺序 ----
    alignment_problems = check_alignment()
    for problem in alignment_problems:
        print(problem, file=sys.stderr)
    print(f"两侧对齐问题 {len(alignment_problems)} 处")
    return 0 if (mismatches == 0 and not alignment_problems) else 1


if __name__ == "__main__":
    sys.exit(main())
