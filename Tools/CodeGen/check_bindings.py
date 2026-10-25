#!/usr/bin/env python3
"""对拍：绑定生成的 C ABI 签名，是否逐行等于 BindableShapes.expected.txt 里写的。

期望文件每行前缀矩阵行号（`01|` … `14|`），随后是期望的 C ABI 签名（逐字）。
生成物在 <产物目录>/<接口>.Bindings.cpp，目录由命令行 `--product-dir` 给（**必填**）。

两件事分开看：
  · 行 11（`拒绝…`）与行 14（`错误码 + 文本…`）不是签名：它们分别由 2.6 的拒绝读数与 3.2 的端到端覆盖，
    这里只统计"有几行是签名"，不参与逐字比对。
  · 产物目录里没有 *.Bindings.cpp 时，脚本打"缺产物"并退出 1 —— 那是生成器未落地时的红态。
    产物**目录本身不存在**是另一回事：说明调用方给错了路径，按"判据自身失效"退出 2。

退出码：
  0 = 所有签名逐行对上
  1 = 有对不上的地方（逐条打印行号、期望、实际），或目录存在但产物缺失
  2 = 判据自身失效（找不到期望文件 / 夹具头，或 --product-dir 指向的目录不存在）
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
EXPECTED_FILE = HERE / "BindableShapes.expected.txt"
FIXTURE_HEADER = HERE / "BindableShapes.h"

ROW_RE = re.compile(r"^([0-9A-Za-z]{2,})\|(.*)$")
# 期望行 -> 符号名：取签名里第一个"标识符 + 左括号"。不再硬编码 `IBindRow\d\d_` ——
# 那样真实接口（`IWindow_*`）的签名一行都进不了比对（实测：改错 IWindow_WindowShouldClose
# 的返回类型，旧脚本照样报"不符数 0"）。
SYMBOL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
DECL_RE = re.compile(r"extern\s+\"C\"[^;{]*?;", re.S)


def read_expected() -> tuple[list[tuple[str, str]], int, set[str]]:
    """返回（签名行列表 [(行号, 期望签名)], 非签名行数, 出现过的全部标签）。

    用 `utf-8-sig` 读：这份文件第一行带 UTF-8 BOM，用 `utf-8` 读会让行首多一个 U+FEFF，
    `ROW_RE` 匹配不上，那一行的签名就**从来没被比对过**（实测：剥 BOM 前"签名 22 行"、剥后 23 行）。
    """
    if not EXPECTED_FILE.is_file():
        raise FileNotFoundError(str(EXPECTED_FILE))
    signatures: list[tuple[str, str]] = []
    special = 0
    rows_seen: set[str] = set()
    for line in EXPECTED_FILE.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        match = ROW_RE.match(line.strip())
        if not match:
            continue
        row, body = match.group(1), match.group(2).strip()
        rows_seen.add(row)
        if body.startswith("拒绝") or body.startswith("错误码"):
            special += 1
            continue
        signatures.append((row, body))
    return signatures, special, rows_seen


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


def read_generated(product_dir: Path) -> dict[str, str]:
    """返回 {符号名: 生成出来的声明（原文）}。"""
    found: dict[str, str] = {}
    if not product_dir.is_dir():
        return found
    for path in sorted(product_dir.glob("*.Bindings.cpp")):
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

# ---- 结构桩判据（任务 1.3）用到的正则 ----
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
STRUCT_RE = re.compile(r"typedef struct (\w+?)Api_v1\s*\{(.*?)\}\s*\1Api_v1\s*;", re.S)
MEMBER_RE = re.compile(r"\(\s*\*\s*(\w+)\s*\)\s*\(")
THUNK_ANCHOR_RE = re.compile(r"static\s+([^\n(]+?)\s+(\w+)_thunk_(\w+)\s*\(")
CALL_RE = re.compile(r"object\s*->\s*(\w+)\s*\(")
VIRTUAL_RE = re.compile(r"virtual\s+([^;(){]+?)\s+(\w+)\s*\(", re.S)
INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)
CA_BIND_CLASS_RE = re.compile(r"CA_BIND\s*\(\s*\)\s*class\s+(\w+)", re.S)


def strip_comments(text: str) -> str:
    """剥掉 `//` 与 `/* */` 注释。

    这一条是判据能成立的前提：桩体里本来就有发射器写的形状注释，不剥的话把那句注释
    改写成提到 `object->` 就能让所有桩一起消失（实测构造：10 个桩全部被喂绿）。
    """
    return COMMENT_RE.sub("", text)


def match_bracket(text: str, index: int, open_ch: str, close_ch: str) -> int:
    """text[index] 是开括号，返回配对闭括号的下标；找不到返回 -1。"""
    depth = 0
    while index < len(text):
        character = text[index]
        if character == open_ch:
            depth += 1
        elif character == close_ch:
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def extract_thunks(text: str) -> dict[str, tuple[str, str]]:
    """返回 {方法名: (投影后的返回类型, thunk 原始函数体)}。

    不用 `[^)]*` 抓形参：回调参数的签名里带 `(*)(...)`，那样会在第一个 `)` 处截断。
    """
    result: dict[str, tuple[str, str]] = {}
    position = 0
    while True:
        match = THUNK_ANCHOR_RE.search(text, position)
        if not match:
            break
        projected = " ".join(match.group(1).split())
        method = match.group(3)
        open_paren = match.end() - 1
        close_paren = match_bracket(text, open_paren, "(", ")")
        if close_paren < 0:
            break
        brace = close_paren + 1
        while brace < len(text) and text[brace].isspace():
            brace += 1
        if brace < len(text) and text[brace] == "{":
            close_brace = match_bracket(text, brace, "{", "}")
            if close_brace > 0:
                result[method] = (projected, text[brace + 1:close_brace])
                position = close_brace + 1
                continue
        position = close_paren + 1
    return result


def table_members(text: str) -> list[str]:
    """按顺序取出 `<接口>Api_v1` 里的成员名（= 进函数表的方法）。"""
    match = STRUCT_RE.search(strip_comments(text))
    if not match:
        return []
    return MEMBER_RE.findall(match.group(2))


def thunk_is_stub(body: str, projected_void: bool) -> bool:
    """函数体里没有真正调用实现，就算桩。四条口径见 design D2。"""
    code = strip_comments(body)
    # ③ 排除 decltype / sizeof / noexcept 上下文与 (void) 前缀丢弃结果
    if re.search(r"\(\s*void\s*\)\s*object\s*->", code):
        return True
    code = re.sub(r"\b(?:decltype|sizeof|noexcept)\s*\([^()]*\)", "", code)
    if not CALL_RE.search(code):
        return True
    if projected_void:
        return False
    # ④ 非 void 投影：调用的结果必须被用（出现在 return 里、或被赋给命名变量）
    return not re.search(r"\breturn\b|[^=!<>+\-*/%&|^]=[^=]", code)


def interface_of(path: Path) -> str:
    return path.name[: -len(".Bindings.cpp")]


def fixture_interface_names() -> set[str]:
    names: set[str] = set()
    for header in sorted(HERE.glob("BindableShapes*.h")):
        names.update(CA_BIND_CLASS_RE.findall(strip_comments(
            header.read_text(encoding="utf-8", errors="replace"))))
    return names


def header_declarations(header: Path) -> dict[str, str]:
    """{方法名: 实现的返回类型拼写}。用于判定"投影成 void、实现却返回非 void"。"""
    text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
    return {name: " ".join(ret.split()) for ret, name in VIRTUAL_RE.findall(text)}


def resolve_interface_header(product_text: str) -> Path | None:
    match = INCLUDE_RE.search(product_text)
    if not match:
        return None
    name = match.group(1)
    local = HERE / name
    if local.is_file():
        return local
    for candidate in sorted(REPO.rglob(name)):
        if candidate.is_file():
            return candidate
    return None


def check_stubs(product_dir: Path) -> tuple[int, int, int, list[str], list[str], list[str]]:
    """返回（真实接口数, 夹具接口数, 夹具桩数, 真实接口问题, 丢返回值问题, 结构问题）。"""
    fixtures = fixture_interface_names()
    products = sorted(product_dir.glob("*.Bindings.cpp"))
    real_paths = [path for path in products if interface_of(path) not in fixtures]
    real_names = {interface_of(path) for path in real_paths}

    real_problems: list[str] = []
    dropped_problems: list[str] = []
    structure_problems: list[str] = []
    fixture_stubs = 0

    for path in products:
        interface = interface_of(path)
        is_real = interface in real_names
        text = path.read_text(encoding="utf-8", errors="replace")
        members = table_members(text)
        thunks = extract_thunks(text)
        header = resolve_interface_header(text)
        declarations = header_declarations(header) if header else {}
        if not members:
            structure_problems.append(f"  {interface}：产物里找不到 <接口>Api_v1 的成员表")
            continue
        for member in members:
            if member not in thunks:
                structure_problems.append(
                    f"  {interface}::{member}：函数表里有这个槽，产物里却没有对应的 thunk")
                continue
            projected, body = thunks[member]
            projected_void = projected == "void"
            if thunk_is_stub(body, projected_void):
                if is_real:
                    real_problems.append(f"  {interface}::{member}：thunk 体内没有真正调用实现（桩）")
                else:
                    fixture_stubs += 1
                continue
            # 独立红条件（design D2 第 4 条）：投影成 void，而实现返回的不是 void ⇒ 调了但丢结果
            if projected_void and member in declarations and declarations[member] != "void":
                if is_real:
                    dropped_problems.append(
                        f"  {interface}::{member}：投影成 void，但实现返回 {declarations[member]}"
                        f" —— 调用了却把返回值丢掉")
                else:
                    fixture_stubs += 1

    return len(real_names), len(products) - len(real_names), fixture_stubs, \
        real_problems, dropped_problems, structure_problems


def check_coverage(product_dir: Path, signatures: list[tuple[str, str]]) -> list[str]:
    """覆盖率下限（任务 1.4）：期望文件里某接口列了哪些方法，产物里就必须正好有这些 thunk。

    没有这一条的话，把方法整段排除掉（`CA_BIND_SKIP` 或从发射循环里过滤）会让 thunk 与
    函数表成员**一起**消失 —— 桩数照样 0、两侧对齐照样 0 处，整个方法静默不见。
    这里逐名比对而不是只比个数：个数相同但换了一个方法也要报出来。
    """
    problems: list[str] = []
    expected_by_interface: dict[str, set[str]] = {}
    for row, body in signatures:
        if row.isdigit():
            continue
        match = SYMBOL_RE.search(body)
        if match:
            expected_by_interface.setdefault(row, set()).add(match.group(1))

    for interface, expected_names in sorted(expected_by_interface.items()):
        path = product_dir / f"{interface}.Bindings.cpp"
        if not path.is_file():
            problems.append(f"  {interface}：期望文件里列了 {len(expected_names)} 个方法，但产物文件不存在")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        actual_names = {f"{interface}_{method}" for method in extract_thunks(text)}
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        if missing:
            problems.append(f"  {interface}：少了 {', '.join(missing)}"
                            f"（期望清单里有、产物里没有对应的 thunk）")
        if extra:
            problems.append(f"  {interface}：多了 {', '.join(extra)}"
                            f"（产物里有、期望清单里没有 —— 要么该补进清单，要么本来就不该进函数表）")
    return problems


def is_companion(name: str) -> bool:
    """伴生函数（两段式 / 释放 / 错误通道）不在"方法集合"里比 —— 它们不是接口方法。"""
    return name.startswith(COMPANION_PREFIXES) or name.endswith(COMPANION_SUFFIXES)


def check_alignment(product_dir: Path) -> list[str]:
    """C++ 侧每个接口的方法名与顺序，必须与 C# 包装类里的一致（任务 1.3）。"""
    problems: list[str] = []
    if not product_dir.is_dir():
        return ["两侧都缺：产物目录不存在，C++ 与 C# 都没有可对的东西"]

    cpp_pairs: dict[str, list[str]] = {}
    cs_pairs: dict[str, list[str]] = {}
    for path in sorted(product_dir.glob("*.Bindings.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for interface, method in CPP_SYMBOL_RE.findall(text):
            if is_companion(method):
                continue
            cpp_pairs.setdefault(interface, []).append(method)
    for path in sorted(product_dir.glob("*.Bindings.cs")):
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


def readme_matrix_rows() -> int:
    """数 `Tools/CodeGen/README.md` 第十三节"投影矩阵"里的数据行（不含表头与分隔行）。

    这个数以前是脚本里写死的字面量 `矩阵行数 14`，不比对任何东西 —— 矩阵漏写一行、
    或加了夹具忘了加矩阵行，它都不会报（任务 2.3 要求交叉核对）。
    """
    readme = REPO / "Tools" / "CodeGen" / "README.md"
    if not readme.is_file():
        return -1
    lines = readme.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    start = next((index for index, line in enumerate(lines) if line.strip() == "### 投影矩阵"), None)
    if start is None:
        return -1
    rows = 0
    started = False
    for line in lines[start + 1:]:
        stripped = line.strip()
        if not stripped.startswith("|"):
            if started:
                break
            continue
        started = True
        if set(stripped.replace("|", "").replace("-", "").strip()) == set():
            continue  # 分隔行
        if "C++ 接口里写的" in stripped:
            continue  # 表头
        rows += 1
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="对拍绑定产物与期望签名")
    parser.add_argument("--product-dir", required=True,
                        help="生成物目录（<构建目录>/codegen/bindings）；必填，判据只认这一个来源")
    args = parser.parse_args()
    product_dir = Path(args.product_dir)
    if not product_dir.is_absolute():
        product_dir = (Path.cwd() / product_dir)
    product_dir = product_dir.resolve()

    if not FIXTURE_HEADER.is_file():
        print(f"找不到夹具头：{FIXTURE_HEADER} —— 判据自身失效", file=sys.stderr)
        return 2
    if not product_dir.is_dir():
        print(f"产物目录不存在：{product_dir} —— 判据自身失效"
              f"（这个退出码 2 与「发现桩」的退出码 1 是分开的）", file=sys.stderr)
        return 2
    try:
        signatures, special, rows_seen = read_expected()
    except FileNotFoundError as exc:
        print(f"找不到期望文件：{exc} —— 判据自身失效", file=sys.stderr)
        return 2

    rows = sorted({row for row, _ in signatures})
    # "有夹具的行数"只数数字标签（矩阵行 01..14，含那一行拒绝夹具）；额外接入的真实接口
    # 用字母标签（`IW|`），不能混进这个数，否则矩阵夹具的覆盖率读数会被真实接口的行数喂大。
    fixture_rows = [row for row in rows_seen if row.isdigit()]
    generated = read_generated(product_dir)

    print(f"产物目录 {product_dir}")
    readme_rows = readme_matrix_rows()
    print(f"矩阵行数 {readme_rows} / 有夹具的行数 {len(fixture_rows)} / "
          f"真实接口行 {len(rows_seen) - len(fixture_rows)} / 签名行 {len(signatures)} / "
          f"非签名行（拒绝或错误码）{special}")
    # 矩阵行数与夹具行号必须一一对应（任务 2.3）：以前那个 14 是写死的字面量，不比任何东西
    if readme_rows < 0:
        print("找不到 README 里的「### 投影矩阵」小节 —— 判据自身失效", file=sys.stderr)
        return 2
    if readme_rows != len(fixture_rows):
        print(f"矩阵行数与夹具行号数不符：README 写 {readme_rows} 行、期望文件里有 "
              f"{len(fixture_rows)} 个行号 —— 加夹具忘了加矩阵行（或反之）", file=sys.stderr)
        return 1

    if not generated:
        print(f"缺产物：{product_dir} 下没有 *.Bindings.cpp —— 生成器还没落地（红态）", file=sys.stderr)
        # 两侧对齐在这种状态下也要有个明确结论，不能因为提前返回就不说话（任务 1.3 的红一）
        for problem in check_alignment(product_dir):
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
    alignment_problems = check_alignment(product_dir)
    for problem in alignment_problems:
        print(problem, file=sys.stderr)
    print(f"两侧对齐问题 {len(alignment_problems)} 处")

    # ---- 结构桩判据（任务 1.3）：真实接口一个桩都不许有；夹具只打印 ----
    real_count, fixture_count, fixture_stubs, stub_problems, dropped_problems, structure_problems = \
        check_stubs(product_dir)
    if real_count == 0:
        print("算不出真实接口 —— 判据自身失效（产物接口减去夹具头里声明的接口后是空集，"
              "「真实接口桩数为 0」会因此恒真）", file=sys.stderr)
        return 2
    real_names = sorted(p.name[: -len(".Bindings.cpp")] for p in product_dir.glob("*.Bindings.cpp")
                        if p.name[: -len(".Bindings.cpp")] not in fixture_interface_names())
    print(f"被判真实接口：{', '.join(real_names)}")
    for problem in structure_problems:
        print(problem, file=sys.stderr)
    for problem in stub_problems:
        print(problem, file=sys.stderr)
    for problem in dropped_problems:
        print(problem, file=sys.stderr)
    print(f"真实接口桩数 {len(stub_problems)} / 丢返回值 {len(dropped_problems)} / "
          f"结构问题 {len(structure_problems)}")
    print(f"夹具接口 {fixture_count} 个、桩数 {fixture_stubs} 个（只打印，不判定）")

    # ---- 覆盖率下限（任务 1.4）：thunk 数必须等于期望文件里该接口的行数 ----
    coverage_problems = check_coverage(product_dir, signatures)
    for problem in coverage_problems:
        print(problem, file=sys.stderr)
    print(f"覆盖率问题 {len(coverage_problems)} 处")

    failed = bool(mismatches or alignment_problems or stub_problems
                  or dropped_problems or structure_problems or coverage_problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
