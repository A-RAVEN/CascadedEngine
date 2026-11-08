#!/usr/bin/env python3
"""原生头投影快照的独立对账（codegen-native-binding-prereqs 任务 4.1）。

为什么要有这个脚本：快照（`evidence/原生头投影快照.txt`）是生成器解析出来的，如果对账也读
生成器的输出，那么"生成器漏采一个方法"会在两侧同时消失、两边永远相等 —— 判据恒真。
所以第二份清单**不走 libclang**：本脚本自己剥注释、取类体、按分号切声明、摘方法名
（与 Tools/CodeGen/check_bindable_audit.py 同族手法），从同一份头文件独立生成。

对账口径：
  · 快照里每个方法行写成 `[采集] 类短名::方法名 | ...` 或 `[排除] 类短名::方法名 | 原因`；
  · 独立清单按"类 → 方法名多重集"（重载保留重复次数）与快照逐条对账；
  · 任一侧多 / 少一个方法都非零并点名；模板类与模板特化两侧都跳过（它们的成员不在
    本批判据的采集面里，且两侧取法不同源）。

红态两例（4.1 执行时实测）：
  ① 给 GPUGraph.h 临时加一个公开方法 ⇒ 独立清单 +1、快照没变 ⇒ 点名该方法；
  ② 手工删快照一行 ⇒ 点名该方法两侧计数不符。
另加下界锚：GPUGraph.h 的独立清单条数 MUST ≥ 23（13 个链式构件 + 10 个访问器，当场重算）。

退出码：
  0 = 两侧逐条一致且下界锚满足
  1 = 有方法对不上（点名类与方法）
  2 = 判据自身失效（快照文件 / 头文件缺失，独立清单解析出 0 个类，或 GPUGraph 条数低于下界锚）
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

HEADERS = [
    "Interface/RenderInterface/header/CRenderBackend.h",
    "Interface/RenderInterface/header/GPUGraph.h",
    "Interface/RenderInterface/header/ShaderStruct.h",
    "Interface/RenderInterface/header/GPUTexture.h",
]
# 下界锚：GPUGraph.h 里 13 个链式构件（AddPass/Rast/Comp/ScheduleData×4/AllocImage/AllocBuffer/
# Present/Finalize/AllocAndUploadBuffer）+ 10 个访问器（GetGraphStages/GetRenderPasses/
# GetRenderPassForWrite/GetComputePasses/GetDataTransfers/GetPassIndices/GetUploadDataHolder/
# GetImageManager/GetBufferManager/GetFinalizePass）＝ 23。独立清单低于这个数说明解析器漏采。
GPUGraph_ANCHOR = 23

COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
METHOD_HEAD_RE = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?(?:inline\s+|static\s+|virtual\s+|explicit\s+|"
                            r"constexpr\s+|const\s+|unsigned\s+|signed\s+)*"
                            r"[A-Za-z_][\w:<>,\s*&]*?\b([A-Za-z_]\w*)\s*\(")
NAME_TAIL_RE = re.compile(r"([A-Za-z_]\w*)\s*$")
# 剥掉函数体 / 枚举体之后仍会带括号但不是方法的词（宏、语句关键字）。
NOT_METHOD = {
    "if", "for", "while", "switch", "return", "do", "else", "catch", "sizeof", "alignof",
    "decltype", "static_assert", "CA_ASSERT", "CA_ASSERT_BREAK", "assert", "defined",
}
DECL_PREFIX = ("friend", "using", "typedef", "namespace", "return", "if", "for", "while",
               "switch", "static_assert", "CA_ASSERT", "else", "do", "case", "catch")


def strip_comments(text: str) -> str:
    """注释里的示例签名会被当成真声明；先剥掉再解析（与 check_bindable_audit 同口径）。"""
    return COMMENT_RE.sub("", text)


def match_brace(text: str, index: int) -> int:
    depth = 0
    for position in range(index, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return position
    return -1


def class_definitions(text: str) -> list[dict]:
    """返回头里每个类 / 结构体定义（含嵌套）：{name, kind, body}。

    跳过 `enum class`（它不是类定义）与模板特化（`struct X<...>`：它的成员与模板类的取法
    两侧不同源，按"本批判据不采模板"跳过）。模板类本身由 `template<...>` 前缀识别后跳过。
    """
    text = strip_comments(text)
    definitions: list[dict] = []
    for match in re.finditer(r"\b(class|struct)\s+(\w+)\b([^{;]*)\{", text):
        # `enum class X {` —— 前面紧邻 enum 的 `class` 不是类定义
        prefix = text[max(0, match.start() - 6):match.start()]
        if prefix.rstrip().endswith("enum"):
            continue
        rest = match.group(3)
        if "<" in rest:
            continue  # 模板特化 / 带模板实参的写法
        # 类名前面 40 个字符里出现 `template<` ⇒ 模板类，跳过
        before = text[max(0, match.start() - 60):match.start()]
        if re.search(r"template\s*<[^>]*>\s*$", before):
            continue
        brace = match.end() - 1
        close = match_brace(text, brace)
        if close < 0:
            continue
        definitions.append({"name": match.group(2), "kind": match.group(1),
                            "body": text[brace + 1:close]})
    return definitions


def remove_nested_bodies(body: str) -> str:
    """把类体里嵌套的 class/struct 定义整段替换成 `;`（它们的成员由各自的类单独对账）。"""
    result = []
    position = 0
    while True:
        match = re.search(r"\b(class|struct)\s+\w+\b[^{;]*\{", body[position:])
        if not match:
            break
        brace = position + match.end() - 1
        close = match_brace(body, brace)
        if close < 0:
            break
        result.append(body[position:brace + 1])
        result.append(";")
        position = close + 1
    result.append(body[position:])
    return "".join(result)


def flatten_bodies(body: str) -> str:
    """把函数体 / 枚举体这类花括号块替换成 `;`，只留下声明文本。"""
    for _ in range(8):
        updated = re.sub(r"\{[^{}]*\}", ";", body)
        if updated == body:
            break
        body = updated
    return body


def public_methods(definition: dict) -> list[str]:
    """类体里的公开方法名（多重集：重载出现几次就记几次；构造函数 / 析构 / 运算符不算）。"""
    body = flatten_bodies(remove_nested_bodies(definition["body"]))
    segments = re.split(r"\b(public|protected|private)\s*:", body)
    visibility = "public" if definition["kind"] == "struct" else "private"
    names: list[str] = []
    for segment in segments:
        if segment in ("public", "protected", "private"):
            visibility = segment
            continue
        if visibility != "public":
            continue
        for declaration in segment.split(";"):
            text = " ".join(declaration.split())
            if not text or "(" not in text:
                continue
            if text.startswith(DECL_PREFIX):
                continue
            match = METHOD_HEAD_RE.match(text)
            if not match:
                continue
            name = match.group(1)
            if name in NOT_METHOD or name.startswith("~") or name.startswith("operator"):
                continue
            if name == definition["name"]:
                continue  # 构造函数不算方法
            names.append(name)
    return names


def independent_lists() -> dict[str, dict[str, list[str]]]:
    """{头相对路径: {类短名: [方法名, …]}}。"""
    result: dict[str, dict[str, list[str]]] = {}
    for header in HEADERS:
        path = REPO / header
        if not path.is_file():
            raise FileNotFoundError(str(path))
        table: dict[str, list[str]] = {}
        for definition in class_definitions(path.read_text(encoding="utf-8", errors="replace")):
            table.setdefault(definition["name"], []).extend(public_methods(definition))
        result[header] = table
    return result


METHOD_LINE_RE = re.compile(r"^\[(采集|排除)\]\s+([A-Za-z_]\w*)::([A-Za-z_]\w*)\s*\|")


def read_snapshot(path: Path) -> dict[str, dict[str, list[str]]]:
    """{头相对路径: {类短名: [方法名, …]}}；只读 [采集]/[排除] 行。"""
    result: dict[str, dict[str, list[str]]] = {}
    current_header: str | None = None
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith("[头] "):
            current_header = stripped[len("[头] "):].strip().replace("\\", "/")
            result.setdefault(current_header, {})
            continue
        match = METHOD_LINE_RE.match(stripped)
        if not match:
            continue
        if current_header is None:
            raise ValueError(f"快照里的方法行出现在任何 [头] 之前：{stripped}")
        result[current_header].setdefault(match.group(2), []).append(match.group(3))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="原生头投影快照独立对账")
    parser.add_argument("--snapshot", required=True, help="快照文件路径")
    args = parser.parse_args()
    snapshot_path = Path(args.snapshot)
    if not snapshot_path.is_file():
        print(f"找不到快照文件：{snapshot_path} —— 判据自身失效", file=sys.stderr)
        return 2
    try:
        independent = independent_lists()
    except FileNotFoundError as error:
        print(f"找不到头文件：{error} —— 判据自身失效", file=sys.stderr)
        return 2
    try:
        snapshot = read_snapshot(snapshot_path)
    except ValueError as error:
        print(f"{error} —— 判据自身失效", file=sys.stderr)
        return 2

    problems: list[str] = []
    # 下界锚：独立清单本身不能漏采（漏采时"两侧相等"是假的）。锚只数 `GPUGraph` 这个类 ——
    # 同一份头里还有 15 个别的类，把它们的条目一起加进来会把锚喂大（实测：全头 89 条）。
    graph_total = len(independent.get(
        "Interface/RenderInterface/header/GPUGraph.h", {}).get("GPUGraph", []))
    print(f"独立清单：GPUGraph.h 方法 {graph_total} 条（下界锚 {GPUGraph_ANCHOR}）")
    if graph_total < GPUGraph_ANCHOR:
        print(f"GPUGraph.h 独立清单只有 {graph_total} 条、低于下界锚 {GPUGraph_ANCHOR} —— "
              f"解析器漏采，判据自身失效", file=sys.stderr)
        return 2

    total = 0
    for header in HEADERS:
        expected = independent[header]
        actual = snapshot.get(header)
        if actual is None:
            problems.append(f"  快照缺少 [头] 段：{header}")
            continue
        for class_name in sorted(set(expected) | set(actual)):
            want = sorted(expected.get(class_name, []))
            got = sorted(actual.get(class_name, []))
            total += len(want)
            if want == got:
                continue
            missing = list(want)
            extra = list(got)
            for name in list(missing):
                if name in extra:
                    missing.remove(name)
                    extra.remove(name)
            if missing:
                problems.append(f"  {header} :: {class_name}：独立清单有、快照没有 {missing}")
            if extra:
                problems.append(f"  {header} :: {class_name}：快照有、独立清单没有 {extra}")
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f"快照对账：独立清单 {total} 条 / 问题 {len(problems)} 处")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
