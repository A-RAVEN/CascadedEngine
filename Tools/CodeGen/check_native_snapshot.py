#!/usr/bin/env python3
"""原生头投影快照的独立对账（codegen-native-binding-prereqs 任务 4.1 / 本批 1.4 扩围）。

为什么要有这个脚本：快照（`evidence/原生头投影快照.txt`）是生成器解析出来的，如果对账也读
生成器的输出，那么"生成器漏采一个方法"会在两侧同时消失、两边永远相等 —— 判据恒真。
所以第二份清单**不走 libclang**：本脚本自己剥注释、取类体、按分号切声明、摘方法名
（与 Tools/CodeGen/check_bindable_audit.py 同族手法），从同一份头文件独立生成。

对账口径（本批 1.4 起，比前置批加严）：
  · 快照里每个方法行写成 `[采集] 类短名::方法名 | ...` 或 `[排除] 类短名::方法名 | 原因`；
  · 独立清单不再只比"类 → 方法名多重集"，而是**逐条比较采集/排除标记**：标记由本脚本按
    spec csharp-bindable-surface 的四类不可绑 + design D2/D8/D9 的例外通道独立算出来
    （`classify_marker`），与快照逐条比 (标记, 类短名, 方法名) 三元组；
  · 任一侧多 / 少一个方法、或同一方法的标记不同，都非零并点名。

红态三例（1.4 执行时实测）：
  ① 给 GPUGraph.h 临时加一个公开方法 ⇒ 独立清单 +1、快照没变 ⇒ 点名该方法；
  ② 手工删快照一行 ⇒ 点名该方法两侧计数不符；
  ③ 把一条已知排除项（`UploadDataHolder::AddData`）的标记从 `[排除]` 翻成 `[采集]` ⇒
     标记不符 ⇒ 点名 —— 前置批只比方法名多重集，这一条翻不动红，本批修掉。
另加下界锚：GPUGraph.h 的独立清单条数 MUST ≥ 23（13 个链式构件 + 10 个访问器，当场重算）。

退出码：
  0 = 两侧逐条一致且下界锚满足
  1 = 有方法 / 标记对不上（点名类与方法）
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
    # 2.4（design D12）：WindowHandle 进表，第五个原生接口
    "Interface/RenderInterface/header/WindowHandle.h",
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

# ---- 标记判定的形状规则（spec csharp-bindable-surface + design D2/D8/D9） ----
INTEGER_TYPES = {
    "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t",
    "size_t", "int", "unsigned", "unsigned int", "long", "unsigned long", "short", "char",
    "signed char", "unsigned char", "long long", "unsigned long long",
}
INJECT_RE = re.compile(r"CA_BIND_INJECT\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
INSTANTIATE_RE = re.compile(r"CA_BIND_INSTANTIATE\s*\(\s*(\w+)\s*,\s*([^)]*)\)")
SKIP_RE = re.compile(r'CA_BIND_SKIP\s*\(\s*(\w+)\s*,')
MAP_CONTAINER_MARKERS = ("unordered_map", "std::map", "castl::map", "std::set", "castl::set",
                         "std::array", "castl::array")


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
        # 类名前面 60 个字符里出现 `template<` ⇒ 模板类，跳过
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


def public_declarations(definition: dict) -> list[dict]:
    """类体里的公开方法声明（多重集；构造函数 / 析构 / 运算符不算）。

    返回 [{name, text, is_template}]，`text` 是整条声明（供独立判定标记用）。
    """
    body = flatten_bodies(remove_nested_bodies(definition["body"]))
    segments = re.split(r"\b(public|protected|private)\s*:", body)
    visibility = "public" if definition["kind"] == "struct" else "private"
    declarations: list[dict] = []
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
            declarations.append({
                "name": name,
                "text": text,
                "is_template": text.lstrip().startswith("template"),
            })
    return declarations


def split_top_level(text: str, separator: str = ",") -> list[str]:
    """按顶层分隔符切分（跳过 <...> / (...) / [...] 内部的分隔符）。"""
    parts: list[str] = []
    depth = 0
    begin = 0
    for index, character in enumerate(text):
        if character in "<([{":
            depth += 1
        elif character in ">)]}":
            depth -= 1
        elif character == separator and depth == 0:
            parts.append(text[begin:index])
            begin = index + 1
    parts.append(text[begin:])
    return [part.strip() for part in parts if part.strip()]


def bare_type(spelling: str) -> str:
    """去掉前导/尾随 const、引用与指针，返回裸类型拼写。"""
    text = spelling.strip()
    changed = True
    while changed:
        changed = False
        if text.startswith("const "):
            text = text[len("const "):].strip()
            changed = True
        if text.endswith(" const"):
            text = text[: -len(" const")].strip()
            changed = True
        while text and text[-1] in "*&":
            text = text[:-1].strip()
            changed = True
    return text


def parameter_name(spelling: str) -> str:
    match = NAME_TAIL_RE.search(spelling)
    return match.group(1) if match else ""


def parameter_type(spelling: str) -> str:
    """把"类型 名字 [= 默认值]"切成类型（快照的声明行带参数名）。"""
    text = spelling.split("=")[0].strip()
    parts = text.rsplit(" ", 1)
    if len(parts) == 2 and re.fullmatch(r"[A-Za-z_]\w*", parts[1]):
        return parts[0].strip()
    return text


def is_const_void_pointer(spelling: str) -> bool:
    # 东/西两种 const 写法都要认：真实头里写的是 `void const* data`，只认 `const void *` 会漏
    # （实测 GPUGraph::ScheduleData 的两条被误判成"裸指针排除"）。
    if "*" not in spelling or "const" not in spelling:
        return False
    return bare_type(spelling.replace("void const", "const void")) == "void"


def is_integer_spelling(spelling: str) -> bool:
    return bare_type(spelling) in INTEGER_TYPES


def serializable_records() -> set[str]:
    """需要序列化块路径的记录短名（成员含容器 / NameHash / PathHash）—— D8 的判定依据。

    独立实现：扫 `Interface/` 下的头，找每个 struct/class 定义体，看成员里有没有
    容器 / 文本成员。与生成器的 `recordNeedsSerialization` 同规则、不同来源。
    """
    records: set[str] = set()
    for path in (REPO / "Interface").rglob("*.h"):
        try:
            text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        for definition in class_definitions(text):
            body = flatten_bodies(definition["body"])
            if any(marker in body for marker in ("vector<", "NameHash", "PathHash")):
                records.add(definition["name"])
    return records


def collect_markers(text: str) -> tuple[dict[tuple[str, str], set[str]], dict[str, dict[str, list[str]]]]:
    """{ (类短名, 方法名): {注入参数名} } 与 { 类短名: {方法名: [实例化类型]} }。"""
    injected: dict[tuple[str, str], set[str]] = {}
    instantiated: dict[str, dict[str, list[str]]] = {}
    current: str | None = None
    # 回退：`CA_BIND_INJECT` / `CA_BIND_INSTANTIATE` 写在类体之后，靠"当前类"逐行推导会串到
    # 下一个类（实测把 ExecuteGraph 的注入记到了 IReadbackToken 头上）。这里按"哪个类体里声明了
    # 这个方法"反推类名，作为 current 不可用时的兜底。
    stripped = strip_comments(text)
    method_owner: dict[str, str] = {}
    for definition in class_definitions(stripped):
        for declaration in public_declarations(definition):
            method_owner.setdefault(declaration["name"], definition["name"])
    for line in stripped.splitlines():
        bind = re.search(r"CA_BIND\s*\(", line)
        if bind:
            # 标注后面最近的 class/struct 名字（同行或下一行）
            following = line[bind.end():]
            name_match = re.search(r"\b(?:class|struct)\s+(\w+)", following)
            current = name_match.group(1) if name_match else None
            continue
        if current is not None:
            name_match = re.search(r"\b(?:class|struct)\s+(\w+)\b", line)
            if name_match and "CA_BIND" not in line:
                current = name_match.group(1)
        for method, parameter in INJECT_RE.findall(line):
            owner = current if current is not None else method_owner.get(method)
            if owner is not None:
                injected.setdefault((owner, method), set()).add(parameter)
        for method, types in INSTANTIATE_RE.findall(line):
            owner = current if current is not None else method_owner.get(method)
            if owner is not None:
                instantiated.setdefault(owner, {}).setdefault(method, []).extend(
                    [part.strip() for part in types.split(",") if part.strip()])
        if re.search(r"\b(class|struct)\s+(\w+)\b[^{;]*\{", line):
            name_match = re.search(r"\b(?:class|struct)\s+(\w+)\b", line)
            if name_match and "CA_BIND" not in line:
                current = name_match.group(1)
    return injected, instantiated


def bound_classes(text: str) -> set[str]:
    """有 `CA_BIND` 标注的类短名（只有这些类的方法会被投影；其余类的方法在快照里一律 排除）。"""
    names: set[str] = set()
    lines = strip_comments(text).splitlines()
    for index, line in enumerate(lines):
        if not re.search(r"CA_BIND\s*\(", line):
            continue
        for offset in (0, 1, 2):
            if index + offset >= len(lines):
                break
            match = re.search(r"\b(?:class|struct)\s+(\w+)", lines[index + offset])
            if match:
                names.add(match.group(1))
                break
    return names


def classify_marker(declaration: dict, class_name: str,
                    injected: dict[tuple[str, str], set[str]],
                    instantiated: dict[str, dict[str, list[str]]],
                    serializable: set[str],
                    bound: set[str] | None = None) -> str:
    """独立算这条声明的快照标记：'采集' 或 '排除'。"""
    text = declaration["text"]
    name = declaration["name"]
    if declaration["is_template"]:
        return "采集" if instantiated.get(class_name, {}).get(name) else "排除"
    open_paren = text.find("(", text.find(name))
    if open_paren < 0:
        return "排除"
    depth = 0
    close_paren = -1
    for index in range(open_paren, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                close_paren = index
                break
    if close_paren < 0:
        return "排除"
    return_spelling = text[:open_paren].strip()
    # 去掉尾部的方法名
    return_spelling = return_spelling[: return_spelling.rfind(name)].strip()
    parameters = split_top_level(text[open_paren + 1: close_paren])
    # 内存窗配对（D2）：`const void*` + 相邻唯一整型长度参数。
    absorbed: set[int] = set()
    is_bound = bound is None or class_name in bound
    for index, parameter in enumerate(parameters):
        parameter_name_text = parameter_name(parameter)
        if parameter_name_text and parameter_name_text in injected.get((class_name, name), set()):
            continue
        if is_bound and is_const_void_pointer(parameter_type(parameter)):
            candidates = [other for other in (index - 1, index + 1)
                          if 0 <= other < len(parameters)
                          and is_integer_spelling(parameter_type(parameters[other]))]
            if len(candidates) == 1:
                absorbed.add(candidates[0])
                continue
            return "排除"
        if "*" in parameter:
            return "排除"
    # 返回位置（D8）：裸指针只允许 `void*`（不透明令牌）与"可序列化记录指针"。
    if "*" in return_spelling:
        return_bare = bare_type(return_spelling)
        # 记录短名在独立清单里不带命名空间；返回位置写的是 `ShaderCompilerSlang::ShaderStructData`。
        return_short = return_bare.split("::")[-1]
        if not (return_bare == "void" and "const" not in return_spelling) \
                and return_short not in serializable:
            return "排除"
    for parameter in parameters:
        if any(marker in parameter for marker in MAP_CONTAINER_MARKERS):
            return "排除"
    if any(marker in return_spelling for marker in MAP_CONTAINER_MARKERS):
        return "排除"
    return "采集"


def independent_lists() -> dict[str, dict[str, list[tuple[str, str]]]]:
    """{头相对路径: {类短名: [(标记, 方法名), …]}}。"""
    serializable = serializable_records()
    result: dict[str, dict[str, list[tuple[str, str]]]] = {}
    for header in HEADERS:
        path = REPO / header
        if not path.is_file():
            raise FileNotFoundError(str(path))
        source = path.read_text(encoding="utf-8", errors="replace")
        injected, instantiated = collect_markers(source)
        bound = bound_classes(source)
        table: dict[str, list[tuple[str, str]]] = {}
        for definition in class_definitions(source):
            for declaration in public_declarations(definition):
                marker = classify_marker(declaration, definition["name"], injected, instantiated,
                                         serializable, bound)
                table.setdefault(definition["name"], []).append((marker, declaration["name"]))
        result[header] = table
    return result


METHOD_LINE_RE = re.compile(r"^\[(采集|排除)\]\s+([A-Za-z_]\w*)::([A-Za-z_]\w*)\s*\|")


def read_snapshot(path: Path) -> dict[str, dict[str, list[tuple[str, str]]]]:
    """{头相对路径: {类短名: [(标记, 方法名), …]}}；只读 [采集]/[排除] 行。"""
    result: dict[str, dict[str, list[tuple[str, str]]]] = {}
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
        result[current_header].setdefault(match.group(2), []).append((match.group(1), match.group(3)))
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
            for entry in list(missing):
                if entry in extra:
                    missing.remove(entry)
                    extra.remove(entry)
            # 同一方法、标记不同 ⇒ 单独点名"标记不符"（前置批的缺口就是这条抓不到）。
            same_name_mismatch: list[str] = []
            for entry in list(missing):
                name = entry[1]
                for other in list(extra):
                    if other[1] == name:
                        same_name_mismatch.append(
                            f"{class_name}::{name}：独立清单判 [{entry[0]}]、快照写 [{other[0]}]")
                        missing.remove(entry)
                        extra.remove(other)
                        break
            for message in same_name_mismatch:
                problems.append(f"  {header} :: 标记不符 —— {message}")
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
