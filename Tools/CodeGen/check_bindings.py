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
        # 回调参数是函数指针（`void (*)(void*, uint32_t)`）：括号里的逗号不是形参分隔符
        elif character == "(":
            depth += 1
        elif character == ")":
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
        # 函数指针形参：去掉 `(*)` 里的名字（生成的声明有名字，期望文件里没有）
        if "(*" in part:
            part = re.sub(r"\(\*\s*\w*\s*\)", "(*)", part)
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
CS_MEMBER_RE = re.compile(r"public static delegate\* unmanaged<.*>\s*(\w+)\s*;")
COMPANION_SUFFIXES = ("Len", "Count", "GetApi")
COMPANION_PREFIXES = ("Copy", "Release", "Resolve", "LastError")

# ---- 结构桩判据（任务 1.3）用到的正则 ----
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
STRUCT_RE = re.compile(r"typedef struct (\w+?)Api_v2\s*\{(.*?)\}\s*\1Api_v2\s*;", re.S)
MEMBER_RE = re.compile(r"\(\s*\*\s*(\w+)\s*\)\s*\(")
# 函数表头部的四个非方法槽（任务 5.10 / 5.2）：错误通道两槽 + 句柄解析/释放两槽。它们不是接口
# 方法、没有对应的 `<接口>_thunk_*`，所以桩判据与覆盖率判据都要把它们排除在外 —— 否则"表里有这个槽、
# 产物里却没有对应的 thunk"会被当成结构缺陷，报出来的是假问题。
CHANNEL_SLOTS = {"LastErrorCode", "LastErrorText", "ResolveHandle", "ReleaseHandle",
                 "ResolveTargetHandle", "ReleaseTargetHandle",
                 "UnregisterCallback", "ReportCallbackError"}

# ---- 共享公开值类型（csharp-render-binding-foundations 任务 1.1 / 2.3）----
# 这类产物**只有** `<类型>.Bindings.cs`，没有对应的 `.Bindings.cpp`：同一个限定 C++ 类型被
# 多个接口使用时只发一份。接口判据（check_surface / check_alignment 第二层）必须跳过它们，
# 由下面这份逐项内容判据接管 —— 否则「没有 IDisposable 表面类」会把它们全判红。
SHARED_VALUE_TYPES = {
    # 渲染基础夹具（本 change 新增）
    "RenderDesc": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct RenderDesc",
        "[MarshalAs(UnmanagedType.I1)]",
        "public bool visible;",
        "public uint id;",
        "public ERenderMode mode;",
        "public ERenderFlag flags;",
        "public ushort weight;",
    ],
    "RenderHashable": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct RenderHashable",
        "public uint first;",
        "public uint second;",
    ],
    "RenderNested": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct RenderNested",
        "public RenderDesc desc;",
        "public float scale;",
    ],
    "ERenderMode": [
        "public enum ERenderMode : ushort",
        "None = 0,",
        "Fill = 1,",
        "Wire = 2,",
    ],
    "ERenderFlag": [
        "[Flags]",
        "public enum ERenderFlag : ushort",
        "None = 0,",
        "Depth = 1,",
        "Blend = 2,",
        "Cull = 4,",
    ],
    # 既有夹具 / 窗口值类型（任务 2.3 把"按接口内嵌的公开记录"改成按类型共享产物）
    "BindScalarThing": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct BindScalarThing",
        "public uint id;",
        "public float weight;",
    ],
    "BindFloatRect": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct BindFloatRect",
        "public float x;",
        "public float y;",
        "public float width;",
        "public float height;",
    ],
    "BindMonitorInfo": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct BindMonitorInfo",
        "public BindFloatRect monitorRect;",
        "public BindFloatRect workAreaRect;",
        "public float dpiScale;",
    ],
    "EBindMode": [
        "public enum EBindMode : byte",
        "None = 0,",
        "One = 1,",
        "Two = 2,",
    ],
    "FloatRect": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct FloatRect",
        "public float x;",
        "public float y;",
        "public float width;",
        "public float height;",
    ],
    "MonitorInfo": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct MonitorInfo",
        "public FloatRect m_MonitorRect;",
        "public FloatRect m_WorkAreaRect;",
        "public float m_DPIScale;",
    ],
    # csharp-render-backend-triangle 任务 4.2：真实引擎公开记录（两个 `uint64_t` 字段按 ulong 镜像）。
    # 它的两个字段是定宽的 `uint64_t`，不是句柄 —— 公开方法里的句柄仍是 `ulong`，照旧拒绝。
    "GPUBufferDescriptor": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct GPUBufferDescriptor",
        "public ulong count;",
        "public ulong stride;",
    ],
    # csharp-render-texture-sampling 任务 4.2：采样器描述记录与它的三个枚举（`SetSampler` 的入参）。
    # 记录是 7 个枚举 + 1 个 bool（bool 必须按 I1 投影，不许退化成 4 字节的默认布尔）。
    "TextureSamplerDescriptor": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct TextureSamplerDescriptor",
        "public ETextureSamplerFilterMode magFilterMode;",
        "public ETextureSamplerFilterMode minFilterMode;",
        "public ETextureSamplerFilterMode mipmapFilterMode;",
        "public ETextureSamplerAddressMode addressModeU;",
        "public ETextureSamplerAddressMode addressModeV;",
        "public ETextureSamplerAddressMode addressModeW;",
        "public ETextureSamplerBorderColor boarderColor;",
        "[MarshalAs(UnmanagedType.I1)]",
        "public bool integerFormat;",
    ],
    "ETextureSamplerFilterMode": [
        "public enum ETextureSamplerFilterMode : int",
        "eNearest = 0,",
        "eLinear = 1,",
    ],
    "ETextureSamplerAddressMode": [
        "public enum ETextureSamplerAddressMode : int",
        "eRepeat = 0,",
        "eMirroredRepeat = 1,",
        "eClampToEdge = 2,",
        "eClampToBorder = 3,",
    ],
    "ETextureSamplerBorderColor": [
        "public enum ETextureSamplerBorderColor : int",
        "eTransparentBlack = 0,",
        "eOpaqueBlack = 1,",
        "eOpaqueWhite = 2,",
    ],
}

# 允许在共享公开记录里出现 `ulong` 的类型（逐项登记，不是整体取消）。
# 判据要求：只允许**记录字段**里的 `uint64_t` 投影；接口方法里的句柄 `ulong` 仍由 check_surface 拒绝。
SHARED_TYPES_ALLOW_ULONG = {"GPUBufferDescriptor"}


REQUIRED_FIXTURE_INTERFACES = {
    # 本 change 新加的夹具接口（任务 4.1）：它们必须仍被 `BindableShapes*.h` 规则识别为夹具，
    # 不能被误当成"真实接口"去要求 IDisposable 表面与桩判据。
    "IBindFoundation",
    "IBindRenderResource",
    "IBindRenderResourceAlt",
    "IBindRenderOwner",
    "IRejectVirtualRecord",
    "IRejectPrivateRecord",
    "IRejectNoDefaultRecord",
    "IRejectNoHashSource",
    "IRejectWrapperReturn",
    "IRejectWrapperNested",
    "IRejectWeakParam",
    "IRejectUnregisteredReturn",
}


def check_fixture_recognition() -> list[str]:
    """确认 `BindableShapes.Render*.h` 都被现有 `BindableShapes*.h` 规则识别为夹具（任务 4.1）。"""
    fixtures = fixture_interface_names()
    return [f"  夹具识别：{name} 没有被 BindableShapes*.h 规则收进夹具集合"
            for name in sorted(REQUIRED_FIXTURE_INTERFACES - fixtures)]


def check_shared_qualified_names(product_dir: Path) -> list[str]:
    """共享公开值类型的限定名唯一性（任务 2.3 / 4.1）。

    每个共享文件头写 `// 限定名：<完整限定名>`。同一短名的文件只能对应一个限定名；反过来，
    同一个限定名也只能有一份短名 —— 短名不同名化会静默覆盖。
    """
    problems: list[str] = []
    short_to_qualified: dict[str, str] = {}
    qualified_to_short: dict[str, str] = {}
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        stem = path.name[: -len(".Bindings.cs")]
        if (product_dir / f"{stem}.Bindings.cpp").is_file():
            continue  # 接口产物，不是共享值类型
        text = path.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"^// 限定名：(.+)$", text, re.M)
        if not match:
            problems.append(f"  共享值类型 {stem}：文件头没有 `// 限定名：...`，无法做限定名对账")
            continue
        qualified = match.group(1).strip()
        previous = short_to_qualified.get(stem)
        if previous is not None and previous != qualified:
            problems.append(f"  共享值类型 {stem}：同一短名对应两个限定名 {previous} / {qualified}")
        short_to_qualified[stem] = qualified
        other = qualified_to_short.get(qualified)
        if other is not None and other != stem:
            problems.append(f"  共享值类型：限定名 {qualified} 同时产出 {other} 与 {stem}")
        qualified_to_short[qualified] = stem
    return problems


def check_shared_value_types(product_dir: Path) -> list[str]:
    """共享公开值类型的内容与唯一性（任务 1.1 登记判据，2.3 / 4.1 扩充）。

    四条口径：
      ① 每个登记的类型必须有一份 `<类型>.Bindings.cs`；缺一份就报；
      ② 文件里的公开类型名必须与文件名一致（防止把 A 的内容写进 B 的文件）；
      ③ 逐字包含登记的那几行 —— 枚举常量值 / 底层类型 / [Flags] / 记录字段名与顺序 /
         bool 的 I1 声明都靠它钉住（真实 CLR 布局另由托管启动检查读 Marshal.SizeOf）；
      ④ 共享文件里不许出现接口内部产物（函数表 / 委托指针 / 句柄 / IDisposable / 异常类）。
    """
    problems: list[str] = []
    for name, required_lines in sorted(SHARED_VALUE_TYPES.items()):
        path = product_dir / f"{name}.Bindings.cs"
        if not path.is_file():
            problems.append(f"  共享值类型 {name}：缺 {path.name}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        declared = re.findall(r"^\s*public\s+(?:sealed\s+)?(?:struct|enum|class)\s+(\w+)",
                              text, re.M)
        if declared != [name]:
            problems.append(f"  共享值类型 {name}：文件里的公开类型是 {declared}，与文件名不符")
        for line in required_lines:
            if line not in text:
                problems.append(f"  共享值类型 {name}：缺少逐字内容 `{line}`")
        # 顺序也必须对：逐行按登记顺序在文件里前进（字段顺序错了但每行都在，只查"包含"会漏）。
        cursor = 0
        for line in required_lines:
            found = text.find(line, cursor)
            if found < 0:
                problems.append(f"  共享值类型 {name}：`{line}` 没有按登记顺序出现（字段/常量顺序错了）")
                break
            cursor = found + len(line)
        for token in ("delegate*", "void*", "IDisposable", "Api_v2", "Exception"):
            if token in text:
                problems.append(f"  共享值类型 {name}：文件里出现了接口内部产物 `{token}`")
        # `ulong` 只对逐项登记的"字段是 uint64_t"的记录放行；其余共享记录照旧拒绝，
        # 免得把"句柄不许出现在公开表面"这条整体取消（任务 4.2 的口径）。
        if name not in SHARED_TYPES_ALLOW_ULONG and re.search(r"\bulong\b", text):
            problems.append(f"  共享值类型 {name}：文件里出现了 `ulong`（只允许登记的 uint64_t 记录字段）")
    # 反向：产物目录里出现的"只有 .cs、没有 .cpp"文件必须都是登记过的共享值类型。
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        stem = path.name[: -len(".Bindings.cs")]
        if (product_dir / f"{stem}.Bindings.cpp").is_file():
            continue
        if stem not in SHARED_VALUE_TYPES:
            problems.append(f"  {stem}：只有 .Bindings.cs 没有 .Bindings.cpp，"
                            f"但它不是登记过的共享值类型")
    return problems


def check_foundation_surface(product_dir: Path) -> list[str]:
    """`IBindFoundation` 的公开表面签名（任务 1.1）。

    这一层断言的是**具体签名整行**，不是方法名：`bool` 返回成 `int`、`GetFlags` 的参数
    还成 `ushort`、包装值还成 `IntPtr`、`byte[]` 还成 `byte*` 都能保持方法名不变，
    只有逐字签名才判得出来。记录 / 枚举的字段内容与真实 CLR 布局由
    `check_shared_value_types` 与托管启动检查负责。
    """
    path = product_dir / "IBindFoundation.Bindings.cs"
    if not path.is_file():
        return ["  IBindFoundation：缺 IBindFoundation.Bindings.cs"]
    text = path.read_text(encoding="utf-8", errors="replace")
    expected = [
        "public RenderDesc GetDesc(RenderDesc desc)",
        "public RenderNested GetNested(RenderNested nested)",
        "public bool GetBool(bool value)",
        "public ERenderMode GetMode(ERenderMode mode)",
        "public ushort GetFlags(ERenderFlag flags)",
        "public void SetName(string name)",
        "public void SetPath(string path)",
        "public void ConsumeHash(RenderHashable value)",
        "public long MixBytes(byte[] data, int count, RenderDesc desc, string label, "
        "ERenderMode mode, ERenderFlag flags, IntPtr token)",
    ]
    problems: list[str] = []
    for signature in expected:
        if signature not in text:
            problems.append(f"  IBindFoundation：公开表面里没有 `{signature}`")
    return problems

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
    """按顺序取出 `<接口>Api_v2` 里的**方法**成员名（不含两个错误通道槽）。"""
    match = STRUCT_RE.search(strip_comments(text))
    if not match:
        return []
    return [name for name in MEMBER_RE.findall(match.group(2)) if name not in CHANNEL_SLOTS]


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
        if STRUCT_RE.search(strip_comments(text)) is None:
            # 区分"表缺失"与"表在、但这个方法不值得进表"：两段式字符串返回与整段
            # CA_BIND_SKIP 的接口表里本来就可以一个方法槽都没有（表头与两个错误通道槽还在）。
            structure_problems.append(f"  {interface}：产物里找不到 <接口>Api_v2 的函数表")
            continue
        for member in members:
            if member not in thunks:
                # 两段式伴生的两个槽（`<方法>Len` / `Copy<方法>`）没有 thunk —— 它们的实现体直接调
                # 接口方法。按命名认它们；"真方法漏了 thunk"由 check_coverage 对着期望文件抓。
                is_two_step = member.endswith("Len") or member.startswith("Copy")
                if is_two_step:
                    continue
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
            symbol = match.group(1)
            # 两段式伴生（`<M>Len` / `Copy<M>`）与配对释放没有 thunk：它们不在覆盖率集合里。
            short = symbol[len(row) + 1:] if symbol.startswith(row + "_") else symbol
            if is_companion(short):
                continue
            expected_by_interface.setdefault(row, set()).add(symbol)

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
    """**只按名字**判伴生 —— 这是粗糙口径，只在拿不到产物符号表时用。

    真实判据在 `check_alignment` 里的 `is_generic`：它拿"产物里真的有哪些符号"来确认
    `<M>Len` / `Copy<M>` 的 `<M>` 确实存在。只按后缀判会把 `GetWindowCount` /
    `GetMonitorCount` 这类**真方法**误当成伴生（它们以 `Count` 结尾），于是那两个方法
    从来没被对齐判据比过。
    """
    return name.startswith(COMPANION_PREFIXES) or name.endswith(COMPANION_SUFFIXES)


def check_alignment(product_dir: Path) -> list[str]:
    """C++ 侧与 C# 侧的对齐，分两层判（任务 1.3 + 5.1）。

    **第一层（native 字段层）**：`extern "C"` 的方法名与顺序 vs `internal` 类里的
    `public static delegate* unmanaged<…> NAME;` 字段。这一层对**所有**产物都判（含夹具），
    也是"原生字段层继续被接受"的那一条 —— 改第二层不许把它判成"C# 缺方法"。

    **第二层（托管表面层，任务 5.1）**：`public` 表面类的**方法声明**必须与 C++ 侧逐个对上，
    并且这个类 MUST 非 static、非 abstract、实现 `IDisposable`。第一层只认 `delegate*` **字段**，
    方法声明一个都不认；"方法属于哪个类、是不是 static"只有第二层判得出来。
    """
    problems: list[str] = []
    if not product_dir.is_dir():
        return ["两侧都缺：产物目录不存在，C++ 与 C# 都没有可对的东西"]

    cpp_pairs: dict[str, list[str]] = {}
    cs_fields: dict[str, list[str]] = {}
    generated = read_generated(product_dir)
    symbols = set(generated)
    # 两段式的基础方法名：`<I>_<M>Len` 存在 ⇒ `M` / `MLen` / `CopyM` 三个都算伴生，不进方法集合
    two_step_bases: dict[str, set[str]] = {}
    for symbol in symbols:
        match = re.fullmatch(r"(\w+?)_(\w+)Len", symbol)
        if match:
            two_step_bases.setdefault(match.group(1), set()).add(match.group(2))

    def is_generic(interface: str, name: str) -> bool:
        """这个符号是"非接口方法"的那一类吗？判据必须按**产物里真的有什么**来认。"""
        if name in ("GetApi", "LastErrorCode", "LastErrorText", "ResolveHandle", "ReleaseHandle",
                    "ResolveTargetHandle", "ReleaseTargetHandle",
                    "UnregisterCallback", "ReportCallbackError"):
            return True
        for base in two_step_bases.get(interface, set()):
            if name in (base, f"{base}Len", f"Copy{base}"):
                return True
        # `<M>` 的配对释放入口（`AcquireThing` → `ReleaseAcquireThing`）
        if name.startswith("Release") and f"{interface}_{name[7:]}" in symbols:
            return True
        return False

    for path in sorted(product_dir.glob("*.Bindings.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for interface, method in CPP_SYMBOL_RE.findall(text):
            if is_generic(interface, method):
                continue
            cpp_pairs.setdefault(interface, []).append(method)
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        interface = path.name[: -len(".Bindings.cs")]
        text = path.read_text(encoding="utf-8", errors="replace")
        for method in CS_MEMBER_RE.findall(text):
            if is_generic(interface, method):
                continue
            cs_fields.setdefault(interface, []).append(method)

    # ---- 第一层：native 字段 ----
    for interface in sorted(set(cpp_pairs) | set(cs_fields)):
        cpp_list = cpp_pairs.get(interface, [])
        cs_list = cs_fields.get(interface, [])
        if cpp_list == cs_list:
            continue
        only_cpp = [name for name in cpp_list if name not in cs_list]
        only_cs = [name for name in cs_list if name not in cpp_list]
        if only_cpp or only_cs:
            problems.append(f"  {interface}（native 字段层）：C++ 多 {only_cpp or '无'}，"
                            f"C# 多 {only_cs or '无'}")
        else:
            problems.append(f"  {interface}（native 字段层）：名字集合相同但顺序不同 —— "
                            f"C++ {cpp_list} / C# {cs_list}")

    # ---- 第二层：托管表面（只判有对应 `.Bindings.cpp` 的接口；共享公开值类型走独立判据，
    # 见 check_shared_value_types —— 任务 4.1 的顺序前提）----
    fixtures = fixture_interface_names()
    interface_stems = set(cpp_pairs) | {path.name[: -len(".Bindings.cpp")]
                                        for path in product_dir.glob("*.Bindings.cpp")}
    for interface in sorted(interface_stems):
        if interface in fixtures:
            continue
        path = product_dir / f"{interface}.Bindings.cs"
        if not path.is_file():
            problems.append(f"  {interface}（表面层）：缺 {interface}.Bindings.cs")
            continue
        blocks = type_blocks(path.read_text(encoding="utf-8", errors="replace"))
        surfaces = [block for block in blocks
                    if block["visibility"] == "public" and block["kind"] == "class"
                    and "IDisposable" in block["rest"]]
        if len(surfaces) != 1:
            problems.append(f"  {interface}（表面层）：`public … : IDisposable` 的类应当正好 1 个，"
                            f"实际 {len(surfaces)} 个")
            continue
        surface = surfaces[0]
        modifiers = surface["mods"].split()
        if "static" in modifiers:
            problems.append(f"  {interface}（表面层）：表面类不许是 static（它要有实例状态）")
        if "abstract" in modifiers:
            problems.append(f"  {interface}（表面层）：表面类不许是 abstract（空壳类不算通过）")
        members = [name for name in PUBLIC_MEMBER_RE.findall(surface["body"]) if name != "Dispose"]
        skip = two_step_bases.get(interface, set())
        members = [name for name in members if name not in skip]
        expected = cpp_pairs.get(interface, [])
        if members == expected:
            continue
        only_expected = [name for name in expected if name not in members]
        only_actual = [name for name in members if name not in expected]
        if only_expected or only_actual:
            problems.append(f"  {interface}（表面层）：C++ 多 {only_expected or '无'}，"
                            f"C# 表面多 {only_actual or '无'}")
        else:
            problems.append(f"  {interface}（表面层）：方法集合相同但顺序不同 —— "
                            f"C++ {expected} / C# {members}")
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


SKIP_RE = re.compile(r'CA_BIND_SKIP\s*\(\s*(\w+)\s*,')
TYPE_DECL_RE = re.compile(
    r"^[ \t]*(?P<vis>public|internal)(?P<mods>[^\n{]*?)\b(?P<kind>class|struct)\s+(?P<name>\w+)(?P<rest>[^\n{]*)"
    r"[ \t]*\n?[ \t]*\{",
    re.M)
PUBLIC_MEMBER_RE = re.compile(r"^[ \t]{8}public\s+[^=;{}]*?\b(\w+)\s*\(", re.M)


def class_body(text: str, class_name: str) -> str | None:
    """按类名切出类体（`header_declarations` 是按整个文件扫的，跨接口会串）。"""
    match = re.search(r"class\s+" + re.escape(class_name) + r"\b[^\n{]*\n?[ \t]*\{", text)
    if not match:
        return None
    brace = match.end() - 1
    close = match_bracket(text, brace, "{", "}")
    return text[brace + 1:close] if close > 0 else None


def type_blocks(text: str) -> list[dict]:
    """把生成物里的类型体切出来 —— 判据必须按**类作用域**判定。"""
    blocks = []
    for match in TYPE_DECL_RE.finditer(text):
        brace = match.end() - 1
        close = match_bracket(text, brace, "{", "}")
        if close < 0:
            continue
        blocks.append({"visibility": match.group("vis"), "mods": match.group("mods"),
                       "kind": match.group("kind"), "name": match.group("name"),
                       "rest": match.group("rest"), "body": text[brace + 1:close]})
    return blocks


def check_surface(product_dir: Path) -> list[str]:
    """托管表面判据（任务 5.2 / 5.6）。

    三条口径：
      ① `public sealed class <接口> : IDisposable` 必须存在，类型名逐字照抄；
      ② **public 类型体里**不许出现 `delegate*` / `void*` / `ulong`（句柄）—— native 层是
         `internal`，按整文件 grep 时它永远命中，判据会恒绿，所以必须按类作用域数；
      ③ 表面成员数（不含 `Dispose`）必须等于「该接口类体里的纯虚方法数 − CA_BIND_SKIP 数」。
         这个数从**接口头**算，不从生成物读 —— 两侧都读生成物就是同源恒等。
    """
    problems: list[str] = []
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        interface = path.name[: -len(".Bindings.cs")]
        # 共享公开值类型（同一次生成里按类型另发的文件）不是接口：它们没有 `.Bindings.cpp`，
        # 也不该有 `IDisposable` 表面类。内容与唯一性由 check_shared_value_types 判。
        if not (product_dir / f"{interface}.Bindings.cpp").is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        blocks = type_blocks(text)
        public_blocks = [block for block in blocks if block["visibility"] == "public"]
        if not public_blocks:
            problems.append(f"  {interface}：.cs 里没有 public 类型（表面根本没生成）")
            continue
        surfaces = [block for block in public_blocks
                    if block["kind"] == "class" and "IDisposable" in block["rest"]]
        if not surfaces:
            problems.append(f"  {interface}：没有 `public sealed class … : IDisposable` 的表面类型")
            continue
        surface = surfaces[0]
        if surface["name"] != interface:
            problems.append(f"  {interface}：表面类型名没有逐字照抄，生成的是 `{surface['name']}`")
        for block in public_blocks:
            for token in ("delegate*", "void*"):
                if token in block["body"]:
                    problems.append(f"  {interface}：public 类型 `{block['name']}` 里出现了 `{token}`")
            if re.search(r"\bulong\b", block["body"]):
                problems.append(f"  {interface}：public 类型 `{block['name']}` 里出现了 `ulong`（句柄）")
        exceptions = [block for block in public_blocks if block["name"] == f"{interface}Exception"]
        if not exceptions:
            problems.append(f"  {interface}：没有 `public sealed class {interface}Exception`")
        elif "Code" not in exceptions[0]["body"]:
            problems.append(f"  {interface}：{interface}Exception 里没有 `Code`")
        members = [name for name in PUBLIC_MEMBER_RE.findall(surface["body"]) if name != "Dispose"]
        # 成员数只在**真实接口**上判：夹具是「C++ → C ABI」那张表的 conformance 夹具，里面
        # 有一半形状（容器、镜像结构体、回调）本来就不在本轮的托管映射表里，生成器会打印
        # 「[托管表面] 跳过 …」并跳过它们（第二张矩阵的夹具由任务 5.7 另配）。
        if interface in fixture_interface_names():
            continue
        product = path.with_name(f"{interface}.Bindings.cpp")
        if not product.is_file():
            problems.append(f"  {interface}：找不到对应的 .Bindings.cpp，算不出期望的方法数")
            continue
        header = resolve_interface_header(product.read_text(encoding="utf-8", errors="replace"))
        if header is None:
            problems.append(f"  {interface}：解析不到接口头，算不出期望的方法数")
            continue
        # 真实接口按头算；夹具（BindableShapes*.h）里一个文件装多个接口，同样按类体算
        body = class_body(strip_comments(header.read_text(encoding="utf-8", errors="replace")), interface)
        if body is None:
            problems.append(f"  {interface}：接口头里找不到 `class {interface}`")
            continue
        expected = len(VIRTUAL_RE.findall(body)) - len(SKIP_RE.findall(body))
        if len(members) != expected:
            problems.append(f"  {interface}：表面成员 {len(members)} 个，期望 {expected} 个"
                            f"（纯虚 {len(VIRTUAL_RE.findall(body))} 条 − 排除 {len(SKIP_RE.findall(body))} 条）")
    return problems


def check_surface_matrix(product_dir: Path) -> list[str]:
    """第二张矩阵（C++ 声明 → 托管表面）的六行 conformance 判据（任务 5.7）。

    每一行点名一个夹具接口 + 一条**具体的表面签名**：生成出来的 `.cs` 里必须逐字出现它。
    第 ①～⑤ 行同时用来覆盖 `void` / `float` / `int` 这些"本来就同名"的形状（不单列行）。
    为什么断言整条签名而不是只比名字：`bool` → `int` 这种映射错，名字一个字都不差。
    """
    rows = [
        ("①", "IBindRow16", "bool 返回 → bool", "public bool GetFlag()"),
        ("②", "IBindRow17", "T& 出参 → 元组返回值", "public (int A, float B) GetPair()"),
        ("③", "IBindRow18", "string const& 入参 → string", "public void SetTitle(string title)"),
        ("④", "IBindRow19", "string_view 返回 → string", "public string GetLabel()"),
        ("⑤", "IBindRow20", "void* → IntPtr", "public IntPtr GetToken()"),
        ("⑥", "IBindRow21", "weak_ptr<T> 返回 → 同名接口引用", "public IBindRow16 GetFlagOwner()"),
    ]
    problems: list[str] = []
    for row, interface, description, signature in rows:
        path = product_dir / f"{interface}.Bindings.cs"
        if not path.is_file():
            problems.append(f"  第二张矩阵 {row} 行（{description}）：缺 {interface}.Bindings.cs")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if signature not in text:
            problems.append(f"  第二张矩阵 {row} 行（{description}）：{interface} 生成的表面里没有 "
                            f"`{signature}` —— 映射写错了")
    return problems


def check_render_bridge_surface(product_dir: Path) -> list[str]:
    """脚本渲染适配面的公开签名与 C ABI 形状（csharp-render-backend-triangle 任务 4.2；
    csharp-render-shader-constants 任务 4.2 追加参数结构接口与三个新方法）。

    两半都判：
      ① 托管表面里逐字出现的方法签名 —— 名字一样但 `byte[]` 退化成 `byte*`、`int` 状态退化成
         `void`、或 `IScriptShaderStruct` 参数换成 `IntPtr`/`ulong`，名字判据都抓不住；
      ② 生成的 `extern "C"` 声明逐字等于期望形状（含 `GPUBufferDescriptorC const*`、
         `uint8_t const* + uint64_t` 的同步字节参数、`SetPassParam` 的目标句柄参数）。
    """
    problems: list[str] = []
    surface_expected = {
        "IScriptRenderTarget.Bindings.cs": [
            "public uint GetWidth()",
            "public uint GetHeight()",
            "public bool IsBackbuffer()",
        ],
        "IScriptRenderGraph.Bindings.cs": [
            "public int AddBuffer(string name, GPUBufferDescriptor descriptor, byte[] data)",
            "public int SetPositionColorStream(string streamName, uint stride, uint positionOffset, "
            "uint colorOffset)",
            "public int SetPositionStream(string streamName, uint stride, uint positionOffset)",
            "public int SetPositionTextureStream(string streamName, uint stride, uint positionOffset, "
            "uint texcoordOffset)",
            "public int SetPassParam(uint passIndex, string blockName, IScriptShaderStruct parameters)",
            "public int AddTrianglePass(IScriptRenderTarget target, string shaderPath, string streamName, "
            "string bufferName, uint vertexCount, float red, float green, float blue, float alpha)",
            "public int Present(IScriptRenderTarget target)",
            "public uint GetPassCount()",
            "public string GetLastError()",
        ],
        "IScriptRenderBackend.Bindings.cs": [
            "public IScriptRenderTarget GetWindowTarget(IWindow window)",
            "public IScriptRenderTarget CreateCaptureTarget(uint width, uint height)",
            "public IScriptRenderGraph CreateGraph()",
            "public IScriptShaderStruct CreateShaderStruct(string structTypeName)",
            "public IScriptTexture CreateTexture(string name, uint width, uint height, byte[] rgba)",
            "public int SubmitGraph(IScriptRenderGraph graph)",
            "public void WaitIdle()",
            "public string GetLastError()",
        ],
        # csharp-render-shader-constants 任务 4.2：第四个脚本接口的完整公开表面。
        # csharp-render-structured-buffer 任务 4.2：追加 `SetBuffer`（两个 string 入参、返回 int）。
        # csharp-render-texture-sampling 任务 4.2：追加 `SetImage` / `SetSampler`（纹理解口表面对象与
        # 公开记录入参，`int` 状态不许退化成 void）。
        "IScriptShaderStruct.Bindings.cs": [
            "public int SetFloat3(string field, float x, float y, float z)",
            "public int SetStruct(string field, IScriptShaderStruct sub, uint elementIndex)",
            "public int SetBuffer(string field, string bufferName)",
            "public int SetImage(string field, IScriptTexture texture)",
            "public int SetSampler(string field, TextureSamplerDescriptor sampler)",
            "public string GetLastError()",
        ],
        # csharp-render-texture-sampling 任务 4.2：第五个脚本接口（只读纹理）的完整公开表面。
        "IScriptTexture.Bindings.cs": [
            "public uint GetWidth()",
            "public uint GetHeight()",
        ],
    }
    for filename, signatures in surface_expected.items():
        path = product_dir / filename
        if not path.is_file():
            problems.append(f"  脚本渲染适配面：缺 {filename}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for signature in signatures:
            if signature not in text:
                problems.append(f"  {filename}：公开表面没有 `{signature}`")

    cpp_expected = {
        "IScriptRenderGraph_AddBuffer":
            'extern "C" int32_t IScriptRenderGraph_AddBuffer(void* self, char const* name, '
            'uint64_t nameLen, GPUBufferDescriptorC const* descriptor, uint8_t const* data, '
            'uint64_t dataCount)',
        "IScriptRenderGraph_SetPositionColorStream":
            'extern "C" int32_t IScriptRenderGraph_SetPositionColorStream(void* self, '
            'char const* streamName, uint64_t streamNameLen, uint32_t stride, uint32_t positionOffset, '
            'uint32_t colorOffset)',
        "IScriptRenderGraph_AddTrianglePass":
            'extern "C" int32_t IScriptRenderGraph_AddTrianglePass(void* self, uint64_t targetHandle, '
            'char const* shaderPath, uint64_t shaderPathLen, char const* streamName, '
            'uint64_t streamNameLen, char const* bufferName, uint64_t bufferNameLen, '
            'uint32_t vertexCount, float red, float green, float blue, float alpha)',
        "IScriptRenderBackend_GetWindowTarget":
            'extern "C" uint64_t IScriptRenderBackend_GetWindowTarget(void* self, uint64_t windowHandle)',
        "IScriptRenderBackend_SubmitGraph":
            'extern "C" int32_t IScriptRenderBackend_SubmitGraph(void* self, uint64_t graphHandle)',
        # csharp-render-shader-constants 任务 4.2：参数结构接口与三个新方法的 C ABI 形状。
        "IScriptRenderGraph_SetPositionStream":
            'extern "C" int32_t IScriptRenderGraph_SetPositionStream(void* self, char const* streamName, '
            'uint64_t streamNameLen, uint32_t stride, uint32_t positionOffset)',
        "IScriptRenderGraph_SetPassParam":
            'extern "C" int32_t IScriptRenderGraph_SetPassParam(void* self, uint32_t passIndex, '
            'char const* blockName, uint64_t blockNameLen, uint64_t parametersHandle)',
        "IScriptRenderBackend_CreateShaderStruct":
            'extern "C" uint64_t IScriptRenderBackend_CreateShaderStruct(void* self, '
            'char const* structTypeName, uint64_t structTypeNameLen)',
        "IScriptShaderStruct_SetFloat3":
            'extern "C" int32_t IScriptShaderStruct_SetFloat3(void* self, char const* field, '
            'uint64_t fieldLen, float x, float y, float z)',
        "IScriptShaderStruct_SetStruct":
            'extern "C" int32_t IScriptShaderStruct_SetStruct(void* self, char const* field, '
            'uint64_t fieldLen, uint64_t subHandle, uint32_t elementIndex)',
        # csharp-render-structured-buffer 任务 4.2：`SetBuffer` 的 C ABI 形状（两个名字各带长度）。
        "IScriptShaderStruct_SetBuffer":
            'extern "C" int32_t IScriptShaderStruct_SetBuffer(void* self, char const* field, '
            'uint64_t fieldLen, char const* bufferName, uint64_t bufferNameLen)',
        # csharp-render-texture-sampling 任务 4.2：纹理创建 / 绑定 / 顶点流的 C ABI 形状。
        # `CreateTexture` 的字节数组沿用 `AddBuffer` 的先例（`uint8_t const* + uint64_t count`）。
        "IScriptRenderBackend_CreateTexture":
            'extern "C" uint64_t IScriptRenderBackend_CreateTexture(void* self, char const* name, '
            'uint64_t nameLen, uint32_t width, uint32_t height, uint8_t const* rgba, '
            'uint64_t rgbaCount)',
        "IScriptShaderStruct_SetImage":
            'extern "C" int32_t IScriptShaderStruct_SetImage(void* self, char const* field, '
            'uint64_t fieldLen, uint64_t textureHandle)',
        "IScriptShaderStruct_SetSampler":
            'extern "C" int32_t IScriptShaderStruct_SetSampler(void* self, char const* field, '
            'uint64_t fieldLen, TextureSamplerDescriptorC const* sampler)',
        "IScriptRenderGraph_SetPositionTextureStream":
            'extern "C" int32_t IScriptRenderGraph_SetPositionTextureStream(void* self, '
            'char const* streamName, uint64_t streamNameLen, uint32_t stride, uint32_t positionOffset, '
            'uint32_t texcoordOffset)',
    }
    generated = read_generated(product_dir)
    for symbol, expected in cpp_expected.items():
        actual = generated.get(symbol)
        if actual is None:
            problems.append(f"  脚本渲染适配面：C ABI 里没有 {symbol}")
        elif normalize(actual) != normalize(expected):
            problems.append(f"  {symbol}：C ABI 形状不符\n    期望 {normalize(expected)}\n    实际 {normalize(actual)}")
    return problems


ERROR_CODE_TABLE = {1001: "STALE_HANDLE", 1002: "NULL_POINTER",
                    1003: "CALLBACK_EXCEPTION", 1004: "CALLBACK_REENTRANT",
                    1005: "CALLBACK_WRONG_THREAD", 1006: "INVALID_MONITOR"}
ERROR_CONST_RE = re.compile(r"static const int32_t (\w+?)_ERROR_(\w+) = (\d+);")
SET_ERROR_RE = re.compile(r"\w+_setError\(\s*[^,]+,\s*([A-Za-z_]\w*|\d+)\s*,")


def check_error_codes(product_dir: Path) -> list[str]:
    """错误码判据（任务 5.8）：生成器发出的每个码都必须在码表里，且不许是裸字面量。

    为什么不能只查表：`setError(…, 1002, …)` 这种裸字面量不点名字，改一个码就漏一处；
    反过来只看常量也不行 —— 常量定义了却没人用，或者用了码表外的数，都判不出来。
    """
    problems: list[str] = []
    for path in sorted(product_dir.glob("*.Bindings.cpp")):
        interface = path.name[: -len(".Bindings.cpp")]
        text = path.read_text(encoding="utf-8", errors="replace")
        constants = {f"{prefix}_ERROR_{name}": int(code)
                     for prefix, name, code in ERROR_CONST_RE.findall(text)}
        for full_name, code in constants.items():
            short_name = full_name.split("_ERROR_", 1)[1]
            if ERROR_CODE_TABLE.get(code) != short_name:
                problems.append(f"  {interface}：常量 {full_name} = {code} 不在码表里"
                                f"（表里 {code} 是 {ERROR_CODE_TABLE.get(code)}）")
        if not constants:
            problems.append(f"  {interface}：产物里一个 `<接口>_ERROR_*` 常量都没有")
        for argument in SET_ERROR_RE.findall(text):
            if argument.isdigit():
                problems.append(f"  {interface}：`setError` 用了裸字面量 {argument}（必须用具名常量）")
            # 全小写的标识符是函数里的局部量（例如把回调错误槽里取出的码转进统一通道），
            # 不是"常量"；生成出来的错误常量一律含大写字母。
            elif argument.isupper() and argument not in constants:
                problems.append(f"  {interface}：`setError` 用了没定义的常量 {argument}")
    return problems


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

    # ---- 托管表面（任务 5.2 / 5.6）：public 类型里不许有 native 细节，成员数要对得上原声明 ----
    surface_problems = check_surface(product_dir)
    for problem in surface_problems:
        print(problem, file=sys.stderr)
    print(f"托管表面问题 {len(surface_problems)} 处")

    # ---- 第二张矩阵的六行 conformance（任务 5.7）：每行断言一条具体的表面签名 ----
    matrix_problems = check_surface_matrix(product_dir)
    for problem in matrix_problems:
        print(problem, file=sys.stderr)
    print(f"第二张矩阵问题 {len(matrix_problems)} 处")

    # ---- 错误码（任务 5.8）：生成器发出的每个码都必须在 design D13 的码表里 ----
    code_problems = check_error_codes(product_dir)
    for problem in code_problems:
        print(problem, file=sys.stderr)
    print(f"错误码问题 {len(code_problems)} 处")

    # ---- 共享公开值类型（任务 1.1 / 2.3）：内容、唯一性与"不许混进接口内部产物" ----
    shared_problems = check_shared_value_types(product_dir)
    for problem in shared_problems:
        print(problem, file=sys.stderr)
    print(f"共享值类型问题 {len(shared_problems)} 处")

    # ---- 共享值类型的限定名对账与夹具识别（任务 2.3 / 4.1）----
    qualified_problems = check_shared_qualified_names(product_dir)
    for problem in qualified_problems:
        print(problem, file=sys.stderr)
    print(f"共享值类型限定名问题 {len(qualified_problems)} 处")
    fixture_problems = check_fixture_recognition()
    for problem in fixture_problems:
        print(problem, file=sys.stderr)
    print(f"夹具识别问题 {len(fixture_problems)} 处")

    # ---- 渲染基础夹具的公开表面签名（任务 1.1）----
    foundation_problems = check_foundation_surface(product_dir)
    for problem in foundation_problems:
        print(problem, file=sys.stderr)
    print(f"渲染基础表面问题 {len(foundation_problems)} 处")

    # ---- 脚本渲染适配面的公开签名与 C ABI 形状（本 change 任务 4.2）----
    render_bridge_problems = check_render_bridge_surface(product_dir)
    for problem in render_bridge_problems:
        print(problem, file=sys.stderr)
    print(f"脚本渲染适配面问题 {len(render_bridge_problems)} 处")

    failed = bool(mismatches or alignment_problems or stub_problems
                  or dropped_problems or structure_problems or coverage_problems or surface_problems
                  or matrix_problems or code_problems or shared_problems or foundation_problems
                  or qualified_problems or fixture_problems or render_bridge_problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
