#!/usr/bin/env python3
"""对账：被审计接口里的每个纯虚方法，是否都在审计表里有且只有一行。

审计表在 openspec/changes/**/evidence/bindable-audit.md（归档后仍在 **/evidence/ 下，所以用通配找，
不写死某一次 change 的目录名）。接口头与"哪个类算被审计对象"写在本文件的 INTERFACES 里。

退出码：
  0 = 对账通过（接口里的每个纯虚方法在表里都有行，且表里没有多余行，且"不可绑"的行都写了理由）
  1 = 有对不上的地方（缺行 / 僵尸行 / 不可绑却没有理由），逐条打印
  2 = 判据自身失效（找不到审计表或某个接口头），此时退出码 1 没有意义
"""
import argparse
import json
import re
import sys
from pathlib import Path

# Windows 控制台默认不是 UTF-8，中文输出会抛 UnicodeEncodeError 把对账结果吞掉
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# 被审计接口 → 它所在的头文件（相对仓库根）
INTERFACES = {
    "CRenderBackend": "Interface/RenderInterface/header/CRenderBackend.h",
    "IReadbackToken": "Interface/RenderInterface/header/CRenderBackend.h",
    "IWindow": "WindowSystem/header/CAWindow/WindowSystem.h",
    "IWindowSystem": "WindowSystem/header/CAWindow/WindowSystem.h",
    "IShaderCompilerManager": "Interface/ShaderCompiler/header/Compiler.h",
    "IOManager": "Interface/IOManager/header/IOManager/IOManager.h",
    "IResource": "GeneralResources/header/CAResource/IResource.h",
}

HEADER_ROW = "| 接口::方法 |"
ROW_RE = re.compile(r"^\|\s*`([A-Za-z_]\w*)::([A-Za-z_]\w*)`\s*\|(.*)$")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
CLASS_RE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)")
# 一条成员声明 = 从 virtual 到下一个分号（声明内部不含分号）
DECL_RE = re.compile(r"\bvirtual\b([^;]*);", re.S)
# 方法名 = 声明里第一个左括号前面的标识符（参数里的括号在它后面）
NAME_RE = re.compile(r"([A-Za-z_]\w*)\s*\(")
PURE_TAIL_RE = re.compile(r"=\s*0\s*$")


def strip_comments(text: str) -> str:
    """注释里的示例签名会被当成真声明（实测：IReadbackToken 的注释里写着 query，IResource 的注释里
    引用了 Serialize 的原文）—— 先剥注释再解析，否则对账表会被注释喂出假方法名。"""
    return COMMENT_RE.sub("", text)


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


def class_body(text: str, class_name: str):
    """返回 class_name 的类体文本；找不到返回 None。按花括号配平，不依赖缩进。"""
    m = re.search(r"\b(?:class|struct)\s+" + re.escape(class_name) + r"\b[^{;]*\{", text)
    if not m:
        return None
    start = m.end() - 1
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    return None


def header_methods(repo: Path, interface: str):
    """解析接口头，返回该类的纯虚方法名列表（含重载，按出现顺序；纯虚析构不算方法）。

    做法：剥注释 → 取类体 → 去掉最外层花括号 → 把"带实现体的成员"（如虚析构的 `{}`）换成空声明 →
    按分号切成声明 → 只留以 `= 0` 结尾的 → 名字取第一个左括号前的标识符。
    这样处理的原因（都是实测踩出来的）：注释里写的示例签名会被当成真声明；虚析构的实现体没有分号，
    直接按分号切会把下一条纯虚方法吞进来；参数里带括号的回调类型会把"第一个括号"判断带偏。
    """
    path = repo / INTERFACES[interface]
    if not path.is_file():
        raise FileNotFoundError(str(path))
    body = class_body(strip_comments(path.read_text(encoding="utf-8", errors="replace")), interface)
    if body is None:
        raise LookupError(interface)
    inner = body.strip()
    if inner.startswith("{"):
        inner = inner[1:]
    if inner.endswith("}"):
        inner = inner[:-1]
    inner = re.sub(r"\{[^{}]*\}", ";", inner)
    names = []
    # 访问标号也不是分号分隔的声明：`public:` 和它下面的第一条方法在同一个"分号块"里，
    # 不先切开就会漏掉"紧跟在 public: 后面的第一条方法"（实测漏了 5 条）。
    decls = []
    for part in re.split(r"\b(?:public|private|protected)\s*:", inner):
        decls.extend(part.split(";"))
    for decl in decls:
        text = decl.strip()
        if not text.startswith("virtual"):
            continue
        text = text[len("virtual") :].strip()
        if not PURE_TAIL_RE.search(text):
            continue
        match = NAME_RE.search(text)
        if match and not match.group(1).startswith("~"):
            names.append(match.group(1))
    return names


def table_rows(audit: Path):
    """只解析主表：从 `| 接口::方法 |` 表头开始，到下一个 `## ` 标题为止。"""
    lines = audit.read_text(encoding="utf-8", errors="replace").splitlines()
    try:
        begin = next(i for i, line in enumerate(lines) if line.startswith(HEADER_ROW))
    except StopIteration:
        raise LookupError("主表表头（| 接口::方法 |）")
    rows = []
    for line in lines[begin + 1 :]:
        if line.startswith("## "):
            break
        if not line.startswith("|"):
            continue
        if set(line.replace("|", "").replace("-", "").strip()) == set():
            continue  # 分隔行
        m = ROW_RE.match(line)
        if not m:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        rows.append(
            {
                "interface": m.group(1),
                "method": m.group(2),
                "cells": cells,
                "line": line,
            }
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="对账可绑定面审计表与接口头里的纯虚方法")
    parser.add_argument("--list", action="store_true", help="打印逐条对账结果")
    parser.add_argument("--json", dest="json_mode", action="store_true", help="机器可读输出（给绑定发射器用）")
    args = parser.parse_args()

    repo = find_repo_root(Path(__file__).resolve())
    audits = sorted(repo.glob("openspec/changes/*/evidence/bindable-audit.md")) + sorted(
        repo.glob("openspec/changes/archive/*/evidence/bindable-audit.md")
    )
    if not audits:
        print("找不到审计表 openspec/changes/**/evidence/bindable-audit.md —— 判据自身失效", file=sys.stderr)
        return 2
    if len(audits) > 1:
        print("找到多份审计表，无法判断以哪份为准：", file=sys.stderr)
        for audit in audits:
            print("  " + str(audit.relative_to(repo)), file=sys.stderr)
        return 2
    audit = audits[0]

    problems = []
    expected = {}  # (interface, method) -> 次数
    try:
        for interface in INTERFACES:
            methods = header_methods(repo, interface)
            if not methods:
                problems.append(f"{interface}：头文件里解析到 0 个纯虚方法 —— 判据自身可能失效")
            for method in methods:
                key = (interface, method)
                expected[key] = expected.get(key, 0) + 1
    except FileNotFoundError as exc:
        print(f"找不到接口头：{exc} —— 判据自身失效", file=sys.stderr)
        return 2
    except LookupError as exc:
        print(f"头文件里找不到类 {exc} —— 判据自身失效", file=sys.stderr)
        return 2

    try:
        rows = table_rows(audit)
    except LookupError as exc:
        print(f"审计表结构不对，找不到 {exc} —— 判据自身失效", file=sys.stderr)
        return 2

    actual = {}
    for row in rows:
        key = (row["interface"], row["method"])
        actual[key] = actual.get(key, 0) + 1
        if key not in expected:
            problems.append(
                f"僵尸行：审计表第里出现了接口中不存在的方法 {row['interface']}::{row['method']}"
            )
        # "不可绑" 必须在"理由或建议"列给出理由（不许留空或只写占位符）
        if len(row["cells"]) >= 7 and row["cells"][5] == "**不可绑**":
            reason = row["cells"][6].strip()
            if len(reason) < 8 or reason in {"—", "-", "无"}:
                problems.append(
                    f"不可绑但没有理由或建议：{row['interface']}::{row['method']}"
                )

    missing = []
    for key, count in expected.items():
        have = actual.get(key, 0)
        if have < count:
            missing.append(f"{key[0]}::{key[1]}（头文件 {count} 条、表里 {have} 条）")
    if missing:
        problems.append("缺少审计行：" + "；".join(missing))

    if args.list:
        print("逐条对账（接口::方法 → 头文件条数 / 表里条数）")
        for key in sorted(set(expected) | set(actual)):
            print(
                f"  {key[0]}::{key[1]} → {expected.get(key, 0)} / {actual.get(key, 0)}"
            )

    total_expected = sum(expected.values())
    if args.json_mode:
        # 给 CodeGen 的绑定发射器用（任务 2.7）：它不能 import Python，只能拿子进程的输出去比对。
        # 字段含义：methods = 头文件里存在、**必须有审计结论**的方法（"接口::方法"）；
        # problems = 对账发现的问题，非空即非零退出；ok = 是否全绿。
        payload = {
            "interfaces": list(INTERFACES),
            "methods": sorted(f"{key[0]}::{key[1]}" for key in expected),
            "total": total_expected,
            "problems": problems,
            "ok": not problems,
        }
        print(json.dumps(payload, ensure_ascii=False))
        return 0 if not problems else 1

    print(
        f"扫描 {len(INTERFACES)} 个接口、对账 {total_expected} 条方法、命中 {len(problems)} 处问题"
        f"（审计表：{audit.relative_to(repo)}）"
    )
    for problem in problems:
        print("  ✗ " + problem)
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
