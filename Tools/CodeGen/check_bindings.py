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
# 那样真实接口（`cawindow__IWindow__*`）的签名一行都进不了比对（实测：改错
# `cawindow__IWindow__WindowShouldClose` 的返回类型，旧脚本照样报"不符数 0"）。
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
        if is_registry_product(path):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for declaration in DECL_RE.findall(text):
            match = SYMBOL_RE.search(declaration)
            if match:
                found[match.group(1)] = declaration
    return found


CPP_SYMBOL_RE = re.compile(r'extern\s+"C"\s+[^;{]*?\b(\w+?)_(\w+)\s*\([^;{]*;', re.S)
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

# ---- 登记表产物（render-interface-merge 任务 1.3）----
# 这三份是**跨接口**产物，不是"一个接口一份"的绑定产物：逐接口判据（桩 / 两侧对齐 / 表面 /
# 错误码 / 共享值类型）必须跳过它们，另由 `check_registry()` 按编号表与 EngineApi 形状判。
REGISTRY_PRODUCT_NAMES = {"BindingRegistry.Bindings.cpp", "BindingRegistry.Bindings.cs",
                          "EngineApi.Bindings.cs"}


def is_registry_product(path: Path) -> bool:
    return path.name in REGISTRY_PRODUCT_NAMES


# ---- 反向产物（csharp-reverse-interface-binding / design D9）----
# 反向产物（C# 实现 C++：公开抽象基类 + C++ 代理）与正向产物（IDisposable 表面类 + Api_v2 函数表）
# 是**两套形状**。生成器在反向产物的第一行写类别标记；四检查据此走替代判据，正向判据一个字不动。
REVERSE_MARKER = "CODEGEN-REVERSE-BINDING:"


def is_reverse_product(path: Path) -> bool:
    """产物是不是反向产物（读生成器写的类别标记）。读不出来按"不是"处理（由对应检查点名缺产物）。"""
    try:
        return REVERSE_MARKER in path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False


def reverse_base_name(short: str) -> str:
    """接口短名 → 公开抽象基类名（去 I 前缀加 Base：IResource → ResourceBase）。"""
    bare = short
    if len(bare) > 1 and bare[0] == "I" and bare[1].isupper():
        bare = bare[1:]
    return bare + "Base"


NAME_BEFORE_PAREN_RE = re.compile(r"([A-Za-z_]\w*)\s*\(")


def reverse_bound_method_names(header: Path, interface: str) -> list[str]:
    """反向接口里"被绑定虚方法"的名字（按声明顺序，跳过 CA_BIND_SKIP）。

    口径与生成器一致：只算 public 段里的 virtual 方法（析构不算）；CA_BIND_SKIP 跳过。
    """
    text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
    body = class_body(text, interface)
    if body is None:
        return []
    skips = {match.group(1) for match in SKIP_RE.finditer(body)}
    inner = body.strip()
    if inner.startswith("{"):
        inner = inner[1:]
    if inner.endswith("}"):
        inner = inner[:-1]
    # 成员函数体（`{}`）先换成 `;`，否则 `virtual ~X() {} virtual void F();` 会被当成一条声明。
    inner = re.sub(r"\{[^{}]*\}", ";", inner)
    names: list[str] = []
    for segment in inner.split(";"):
        decl = segment.strip()
        if "virtual" not in decl or "(" not in decl:
            continue
        # `CA_BIND_SKIP(方法, "理由")` 不是方法声明：宏没写分号时它会和后面的 virtual 声明
        # 粘成一段，NAME_BEFORE_PAREN_RE 会把 "CA_BIND_SKIP" 当成方法名（实测过）。
        decl = re.sub(r"CA_BIND_(?:SKIP|INJECT|INSTANTIATE)\s*\([^)]*\)", " ", decl)
        match = NAME_BEFORE_PAREN_RE.search(decl)
        if not match:
            continue
        name = match.group(1)
        if name in skips or name == interface or name.startswith("~") or name.startswith("CA_"):
            continue
        names.append(name)
    return names


def find_abstract_base(text: str, short: str) -> dict | None:
    """在反向 .cs 里找公开抽象基类（返回 type_blocks 的那一项）。"""
    expected = reverse_base_name(short)
    for block in type_blocks(text):
        if block["visibility"] == "public" and block["kind"] == "class" \
                and "abstract" in block["mods"].split() and block["name"] == expected:
            return block
    return None


def check_reverse_base(product_dir: Path) -> tuple[int, list[str]]:
    """反向公开抽象基类的干净形状（design D9 / csharp-bindings 的 check_reverse_base）。

    递归 glob 扫产物目录下所有 `.Bindings.cs`（夹具在子目录里也要扫到），只看带反向类别标记的。
    判据：① 公开抽象基类恰好一个、名字 = 接口短名去 I 加 Base；② 它（以及同文件其它 public 类型）的
    签名里没有 `void*` / `delegate*` / 句柄形态的 `ulong`；③ abstract 方法数与名字顺序 = 被绑定虚方法；
    ④ 成对的代理 `.cpp` 存在（反向两件套）。返回（命中文件数, 问题列表）—— 命中数 0 时调用方判红，
    避免"扫了个空目录也绿"。
    """
    problems: list[str] = []
    hits = 0
    for path in sorted(product_dir.rglob("*.Bindings.cs")):
        if not is_reverse_product(path):
            continue
        hits += 1
        linear = path.name[: -len(".Bindings.cs")]
        short = split_linear(linear)[1]
        text = path.read_text(encoding="utf-8", errors="replace")
        expected_base = reverse_base_name(short)
        public_blocks = [block for block in type_blocks(text) if block["visibility"] == "public"]
        abstracts = [block for block in public_blocks
                     if block["kind"] == "class" and "abstract" in block["mods"].split()]
        if len(abstracts) != 1:
            problems.append(f"  {short}（反向基类）：public abstract class 应当正好 1 个，"
                            f"实际 {[block['name'] for block in abstracts]}")
            continue
        base = abstracts[0]
        if base["name"] != expected_base:
            problems.append(f"  {short}（反向基类）：基类名是 `{base['name']}`，"
                            f"期望 `{expected_base}`（接口短名去 I 前缀加 Base）")
        for block in public_blocks:
            for token in ("void*", "delegate*"):
                if token in block["body"]:
                    problems.append(f"  {short}（反向基类）：public 类型 `{block['name']}` 里出现了 `{token}`")
            # 句柄形态判据只看**公开成员**：public 类里的 internal 字段也是实现细节，但只要它带
            # `ulong xxxHandle` 就会被逐行扫描误判 —— 反向基类本来就不许有字段，这里按可见性过滤，
            # 让"字段藏 internal 桥"成为唯一正确解法（不是靠改名绕过）。
            public_body = "\n".join(
                line for line in block["body"].splitlines()
                if not line.strip().startswith(("internal ", "private ", "protected ")))
            problems.extend(public_ulong_problems(short, {**block, "body": public_body}))
        abstract_methods = re.findall(r"^\s*public abstract [^;{]*?\b(\w+)\s*\(", base["body"], re.M)
        product = product_dir / f"{linear}.Bindings.cpp"
        if not product.is_file():
            problems.append(f"  {short}（反向产物）：缺成对的 {linear}.Bindings.cpp（代理没生成）")
            continue
        header = resolve_interface_header(product.read_text(encoding="utf-8", errors="replace"))
        if header is None:
            problems.append(f"  {short}（反向基类）：解析不到接口头，算不出被绑定虚方法数")
            continue
        expected_names = reverse_bound_method_names(header, short)
        if abstract_methods != expected_names:
            problems.append(f"  {short}（反向基类）：abstract 方法 {abstract_methods} 与被绑定虚方法 "
                            f"{expected_names} 不自洽（顺序也要一致）")
    return hits, problems


def check_reverse_alignment(product_dir: Path) -> list[str]:
    """反向产物两侧对齐：C++ 代理覆写的虚方法 vs C# 抽象基类的 abstract 方法（同序同名）。

    正向对齐判的是"Api_v2 槽 ↔ delegate* 字段"；反向没有函数表字段，替而判"代理 override ↔
    抽象方法"。跳过的方法（CA_BIND_SKIP）代理里也有一份抛异常的实现，但它不在 C# 抽象基类里。
    """
    problems: list[str] = []
    for product in sorted(product_dir.glob("*.Bindings.cpp")):
        if not is_reverse_product(product):
            continue
        linear = product.name[: -len(".Bindings.cpp")]
        short = split_linear(linear)[1]
        cs_path = product_dir / f"{linear}.Bindings.cs"
        if not cs_path.is_file():
            problems.append(f"  {short}（反向对齐）：缺 {linear}.Bindings.cs")
            continue
        cs_text = cs_path.read_text(encoding="utf-8", errors="replace")
        base = find_abstract_base(cs_text, short)
        if base is None:
            problems.append(f"  {short}（反向对齐）：.cs 里找不到公开抽象基类")
            continue
        abstract_methods = re.findall(r"^\s*public abstract [^;{]*?\b(\w+)\s*\(", base["body"], re.M)
        cpp_text = product.read_text(encoding="utf-8", errors="replace")
        proxy_methods = []
        for match in re.finditer(
                r"^\s{4}[^;{]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?override\b", cpp_text, re.M):
            # 析构（`~<线形>Proxy() override`）不是接口方法：正则会把 `~` 后面的标识符当方法名。
            if "~" in match.group(0):
                continue
            proxy_methods.append(match.group(1))
        header = resolve_interface_header(cpp_text)
        skips: set[str] = set()
        if header is not None:
            header_text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
            skips = set(re.findall(r"CA_BIND_SKIP\s*\(\s*(\w+)\s*,", header_text))
        bound_proxy_methods = [name for name in proxy_methods if name not in skips]
        if bound_proxy_methods != abstract_methods:
            problems.append(f"  {short}（反向对齐）：C++ 代理覆写 {bound_proxy_methods} / "
                            f"C# abstract {abstract_methods} —— 方法集合或顺序不一致")
    return problems


def check_reverse_stubs(product_dir: Path) -> list[str]:
    """反向代理的非空体判据（design D9：反向用行为判据，不用 `object->` 结构判据）。

    这里只判"结构不空"：代理存在、每个 override 体里真的调了函数指针槽（跳过的方法允许抛
    ReverseBindingError）、析构里确实调了 releaseUser。**行为**（C# 覆写真的被调到、返回值收到）
    由 GPUBackendTester 的 TestCSharpResourceFactory 与 CoreTests 的反向夹具承担 —— 空体在那里会红。
    """
    problems: list[str] = []
    for product in sorted(product_dir.glob("*.Bindings.cpp")):
        if not is_reverse_product(product):
            continue
        short = split_linear(product.name[: -len(".Bindings.cpp")])[1]
        text = product.read_text(encoding="utf-8", errors="replace")
        if "Proxy final : public" not in text:
            problems.append(f"  {short}（反向代理）：产物里没有代理类")
            continue
        for match in re.finditer(
                r"^\s{4}[^;{]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?override\s*\n\s*\{", text, re.M):
            if "~" in match.group(0):
                continue   # 析构不是接口方法（它的 releaseUser 另外单独判）
            name = match.group(1)
            body = text[match.end():]
            end = body.find("\n    }")
            body = body[: end if end >= 0 else len(body)]
            if "m_slots." not in body and "ReverseBindingError" not in body:
                problems.append(f"  {short}::{name}：代理方法体没有调用函数指针槽（空体/桩）")
        if "release(m_user)" not in text:
            problems.append(f"  {short}（反向代理）：析构里没有调 releaseUser（托管 GCHandle 不会释放）")
    return problems


# ---- 编号锁文件（codegen-native-binding-prereqs 任务 1.3）----
# 编号的唯一来源是入库锁文件 `Tools/CodeGen/interface-ids.lock`：本脚本不再保留手抄编号表。
# 锁缺失 / 格式不对 = 判据自身失效（退出 2）；锁与产物对不上 = 发现差异（退出 1）。
LOCK_FILE = HERE / "interface-ids.lock"


def read_lock_scope(scope: str) -> dict[int, str]:
    """读锁文件里指定段的 {编号: 完整限定名}；格式不对抛 ValueError（调用方按退出 2 处理）。"""
    if not LOCK_FILE.is_file():
        raise ValueError(f"找不到编号锁文件：{LOCK_FILE}")
    numbers: dict[int, str] = {}
    current: str | None = None
    for line in LOCK_FILE.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        text = line.rstrip()
        if not text.strip():
            continue
        if text.startswith("# scope "):
            current = text[len("# scope "):].strip()
            continue
        if text.lstrip().startswith("#"):
            continue
        fields = text.split("\t")
        if len(fields) != 4:
            raise ValueError(f"锁文件数据行不是四列（编号、短名、完整限定名、头路径）：{text!r}")
        if current != scope:
            continue
        try:
            number = int(fields[0])
        except ValueError as error:
            raise ValueError(f"锁文件里的编号不是整数：{text!r}") from error
        if number <= 0:
            raise ValueError(f"锁文件里的编号不是正整数：{text!r}")
        if number in numbers:
            raise ValueError(f"锁文件段 {scope} 有重复编号 {number}")
        numbers[number] = fields[2]
    if not numbers:
        raise ValueError(f"锁文件里没有段 {scope} 的数据行")
    return numbers


# `EngineApi` 的 9 个字段（C++ 名 / C# 名，顺序就是 ABI；6.2 的 v10 重建：桥五类编号字段已删）。两侧必须逐字段同序。
ENGINE_API_FIELD_PAIRS = [
    ("abi_version", "AbiVersion"),
    ("size", "Size"),
    ("log", "Log"),
    ("echo", "Echo"),
    ("get_interface_api", "GetInterfaceApi"),
    ("window_interface_id", "WindowInterfaceId"),
    ("window_system_interface_id", "WindowSystemInterfaceId"),
    ("get_binding_layout_type", "GetBindingLayoutType"),
    ("get_binding_layout_field", "GetBindingLayoutField"),
]
# 逐字段的**逐字声明**（C++ 侧）：除了名字，宽度与函数指针形状也必须同 v8 手写表一致。
# 只数名字会放过"函数指针把名字写了两遍"这种编译不过的产物（实际踩过一次）。
ENGINE_API_FIELD_DECLARATIONS = [
    "uint32_t abi_version;",
    "uint32_t size;",
    "void (*log)(int level, uint8_t const* utf8, uint64_t len);",
    "int32_t (*echo)(int32_t value);",
    "uint32_t (*get_interface_api)(uint32_t interfaceId, char const* instanceName, uint32_t version, "
    "uint32_t size, void* out, void** instanceOut);",
    "uint32_t window_interface_id;",
    "uint32_t window_system_interface_id;",
    "uint32_t (*get_binding_layout_type)(uint32_t interfaceId, uint32_t typeIndex, char* name, "
    "uint32_t nameCap, uint64_t* sizeBytes, uint32_t* fieldCount);",
    "uint32_t (*get_binding_layout_field)(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex, "
    "char* name, uint32_t nameCap, char* typeName, uint32_t typeCap, uint64_t* offsetBytes);",
]

# ---- 共享公开值类型（csharp-render-binding-foundations 任务 1.1 / 2.3）----
# 这类产物**只有** `<类型>.Bindings.cs`，没有对应的 `.Bindings.cpp`：同一个限定 C++ 类型被
# 多个接口使用时只发一份。接口判据（check_surface / check_alignment 第二层）必须跳过它们，
# 由下面这份逐项内容判据接管 —— 否则「没有 IDisposable 表面类」会把它们全判红。
SHARED_VALUE_TYPES = {
    # csharp-native-direct-binding 2.2：原生四类进表后带出的共享值类型（句柄值类 / 纹理面值类 /
    # 枚举 / 12 字节 float3）。只钉"这个文件属于预期产物 + 它的公开类型名逐字"，细节由生成侧保证。
    "ImageHandle": ["public sealed class ImageHandle"],
    "BufferHandle": ["public sealed class BufferHandle"],
    # 原生四类带出的其余产物（着色器元数据记录 / 描述符用的枚举）。只钉"这个文件属于预期产物"。
    "BufferData": ["public struct BufferData"],
    "ShaderTextureData": ["public struct ShaderTextureData"],
    "SubStructReference": ["public struct SubStructReference"],
    "UniformElement": ["public struct UniformElement"],
    "ShaderStructData": ["public sealed class ShaderStructData"],
    "ShaderStructUniforms": ["public sealed class ShaderStructUniforms"],
    "GPUTimingCapabilities": ["public struct GPUTimingCapabilities"],
    "EShaderResourceAccess": ["public enum EShaderResourceAccess : byte"],
    "EBufferUsage": ["public enum EBufferUsage : ushort"],
    "EGPUQueueType": ["public enum EGPUQueueType : uint"],
    "ETextureAccessType": ["public enum ETextureAccessType : byte"],
    # 2.4（D12）：WindowHandle::GetSizeSafe 的纯标量记录
    "uint2": ["public struct uint2"],
    # 原生图 / 管线 / 附件记录的托管镜像（2.2 进表后带出）。只钉"这个文件属于预期产物"。
    "RenderPass": ["public sealed class RenderPass"],
    "DrawCallBatch": ["public struct DrawCallBatch"],
    "DrawCall": ["public struct DrawCall"],
    "DrawInfo": ["public sealed class DrawInfo"],
    "IndexBufferData": ["public sealed class IndexBufferData"],
    "PipelineDescData": ["public sealed class PipelineDescData"],
    "InputAssemblyStates": ["public sealed class InputAssemblyStates"],
    "RasterizerStates": ["public sealed class RasterizerStates"],
    "DepthStencilStates": ["public sealed class DepthStencilStates"],
    "StencilStates": ["public sealed class StencilStates"],
    "ColorAttachmentsBlendStates": ["public sealed class ColorAttachmentsBlendStates"],
    "SingleColorAttachmentBlendStates": ["public sealed class SingleColorAttachmentBlendStates"],
    "CPipelineStateObject": ["public sealed class CPipelineStateObject"],
    "RectSate": ["public sealed class RectSate"],
    "ViewRectData": ["public sealed class ViewRectData"],
    "ShaderInfo": ["public sealed class ShaderInfo"],
    "AttachmentConfig": ["public struct AttachmentConfig"],
    "ComputeBatch": ["public sealed class ComputeBatch"],
    "ComputeDispatch": ["public struct ComputeDispatch"],
    "VertexAttribute": ["public struct VertexAttribute"],
    "VertexInputsDescriptor": ["public sealed class VertexInputsDescriptor"],
    "EAttachmentLoadOp": ["public enum EAttachmentLoadOp : int"],
    "EAttachmentStoreOp": ["public enum EAttachmentStoreOp : int"],
    "EBlendFactor": ["public enum EBlendFactor : int"],
    "EBlendOp": ["public enum EBlendOp : int"],
    "EColorChannelMask": ["public enum EColorChannelMask : byte"],
    "ECompareOp": ["public enum ECompareOp : int"],
    "ECullMode": ["public enum ECullMode : int"],
    "EFrontFace": ["public enum EFrontFace : int"],
    "EIndexBufferType": ["public enum EIndexBufferType : byte"],
    "EPolygonMode": ["public enum EPolygonMode : int"],
    "EStencilOp": ["public enum EStencilOp : int"],
    "ETopology": ["public enum ETopology : int"],
    "VertexInputFormat": ["public enum VertexInputFormat : int"],
    "float3": ["public struct float3"],
    "GPUTextureDescriptor": ["public struct GPUTextureDescriptor"],
    "GPUTextureView": ["public struct GPUTextureView"],
    "GPUTextureSwizzle": ["public struct GPUTextureSwizzle"],
    "ETextureFormat": ["public enum ETextureFormat : byte"],
    "ETextureType": ["public enum ETextureType : byte"],
    "ETextureAspect": ["public enum ETextureAspect : byte"],
    "EColorChannel": ["public enum EColorChannel : byte"],
    "EMultiSampleCount": ["public enum EMultiSampleCount : int"],
    # csharp-native-direct-binding 任务 1.5 / design D11：图形状序列化记录（三类元素一次过界）
    "GraphLikeRecord": [
        "public sealed class GraphLikeRecord",
        "public NativeResourceRef[] m_Refs;",
        "public System.Collections.Generic.Dictionary<string, uint> m_Counts;",
        "public System.Collections.Generic.Dictionary<string, PassAttachment> m_Attachments;",
        "public System.Collections.Generic.Dictionary<string, NativeResourceRef> m_RefsByName;",
        "public IBindNativeShape m_Object;",
        "public System.Collections.Generic.Dictionary<string, IBindNativeShape> m_Objects;",
        "internal static class GraphLikeRecordCodec",
        "public static byte[] Serialize(GraphLikeRecord value)",
    ],
    # codegen-native-binding-prereqs 任务 3.2：序列化块记录与容器元素记录
    "PassLike": [
        "public sealed class PassLike",
        "public PassAttachment[] m_Attachments;",
        "public EPassMode[] m_Modes;",
        "public string m_Name;",
        "public uint m_PrivateCount;",
        "internal static class PassLikeCodec",
        "public const uint FormatVersion = 1u;",
        "public static byte[] Serialize(PassLike value)",
    ],
    "PassAttachment": [
        "public struct PassAttachment",
        "public uint index;",
        "public float weight;",
    ],
    "EPassMode": [
        "public enum EPassMode : ushort",
        "Fill = 0,",
        "Wire = 1,",
    ],
    # codegen-native-binding-prereqs 任务 3.1：资源引用值类的共享托管类型
    "NativeResourceRef": [
        "public sealed class NativeResourceRef",
        "internal readonly uint Kind;",
        "internal readonly ulong Handle;",
        "internal readonly string Name;",
        "public static NativeResourceRef FromBindNativeShape(IBindNativeShape value)",
        "public static NativeResourceRef FromName(string name)",
    ],
    # codegen-native-binding-prereqs 任务 2.3：静态工厂返回的纯标量记录（本批新增）
    "NativeCount": [
        "[StructLayout(LayoutKind.Sequential)]",
        "public struct NativeCount",
        "public uint calls;",
        "public uint lastValue;",
    ],
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
SHARED_TYPES_ALLOW_ULONG = {"GPUBufferDescriptor", "NativeResourceRef", "PassLike", "GraphLikeRecord",
                            # 句柄值类：它们的 `ulong Handle` 就是句柄本身（值类的定义如此）。
                            "ImageHandle", "BufferHandle",
                            # 元数据记录里有 `uint64_t` 字段（布局哈希/大小由原生决定）。
                            "ShaderStructUniforms", "ShaderStructData",
                            # 原生记录里的 `cacore::HashObj<T>` 的哈希值就是 64 位（字段名 `m_Value`
                            # 在 C# 镜像里是 `ulong`）。
                            "CPipelineStateObject", "ColorAttachmentsBlendStates", "ComputeBatch",
                            "PipelineDescData", "InputAssemblyStates", "RasterizerStates",
                            "DepthStencilStates", "RectSate", "ViewRectData", "ComputeDispatch",
                            "DrawCallBatch"}


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
    # codegen-native-binding-prereqs 任务 2.1：原生形态夹具（不进真实宿主登记表）。
    "IBindNativeShape",
    "BindNativeObject",
}


def check_fixture_recognition() -> list[str]:
    """确认 `BindableShapes.Render*.h` 都被现有 `BindableShapes*.h` 规则识别为夹具（任务 4.1）。"""
    fixtures = fixture_interface_names()
    return [f"  夹具识别：{name} 没有被 BindableShapes*.h 规则收进夹具集合"
            for name in sorted(REQUIRED_FIXTURE_INTERFACES - fixtures)]


def check_shared_qualified_names(product_dir: Path) -> list[str]:
    """共享公开值类型的限定名唯一性（任务 2.3 / 4.1）。

    每个共享文件头写 `// 限定名：<完整限定名>`。文件名必须是该限定名的线形（本批 D6）；
    同一个线形只能对应一个限定名 —— 否则磁盘上会静默覆盖。
    """
    problems: list[str] = []
    linear_to_qualified: dict[str, str] = {}
    qualified_to_linear: dict[str, str] = {}
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        if is_registry_product(path):
            continue
        stem = path.name[: -len(".Bindings.cs")]
        if (product_dir / f"{stem}.Bindings.cpp").is_file():
            continue  # 接口产物，不是共享值类型
        text = path.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"^// 限定名：(.+)$", text, re.M)
        if not match:
            problems.append(f"  共享值类型 {stem}：文件头没有 `// 限定名：...`，无法做限定名对账")
            continue
        qualified = match.group(1).strip()
        expected = python_linear(qualified)
        if stem != expected:
            problems.append(f"  共享值类型 {stem}：文件名不是限定名线形（{qualified} → {expected}）")
        previous = linear_to_qualified.get(stem)
        if previous is not None and previous != qualified:
            problems.append(f"  共享值类型 {stem}：同一线形对应两个限定名 {previous} / {qualified}")
        linear_to_qualified[stem] = qualified
        other = qualified_to_linear.get(qualified)
        if other is not None and other != stem:
            problems.append(f"  共享值类型：限定名 {qualified} 同时产出 {other} 与 {stem}")
        qualified_to_linear[qualified] = stem
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
        path = product_by_interface(product_dir, name, ".Bindings.cs")
        if path is None:
            problems.append(f"  共享值类型 {name}：缺 <限定名线形>.Bindings.cs")
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
        for token in ("delegate*", "void*", "IDisposable", "Api_v2"):
            if token in text:
                problems.append(f"  共享值类型 {name}：文件里出现了接口内部产物 `{token}`")
        # 异常类只挡"文件里自己定义了一个异常类型"（接口产物 `<接口>Exception` 的形态）；
        # 编解码里 `throw new System.IO.InvalidDataException(...)` 这类 BCL 异常是正常用法
        # （D8 的 Deserialize 要用），不能按 "Exception" 这个词一刀切。
        if re.search(r"\bclass\s+\w+Exception\b", text):
            problems.append(f"  共享值类型 {name}：文件里定义了异常类型（接口内部产物 `class …Exception`）")
        # `ulong` 只对逐项登记的"字段是 uint64_t"的记录放行；**生成的数据记录镜像**（带 Codec 的
        # 序列化块记录 / 元素记录）整体豁免：它们的 `ulong` 是数据字段（uint64 计数、NameHash 哈希、
        # HashObj 的值），不是句柄。句柄类的公开表面（接口产物 / 托管值类）仍然照旧拒绝。
        is_data_record = "Codec" in text and "internal static class " in text
        if name not in SHARED_TYPES_ALLOW_ULONG and not is_data_record and re.search(r"\bulong\b", text):
            problems.append(f"  共享值类型 {name}：文件里出现了 `ulong`（只允许登记的 uint64_t 记录字段）")
    # 反向：产物目录里出现的"只有 .cs、没有 .cpp"文件必须都是登记过的共享值类型。
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        if is_registry_product(path):
            continue
        stem = path.name[: -len(".Bindings.cs")]
        if (product_dir / f"{stem}.Bindings.cpp").is_file():
            continue
        if split_linear(stem)[1] not in SHARED_VALUE_TYPES:
            problems.append(f"  {stem}：只有 .Bindings.cs 没有 .Bindings.cpp，"
                            f"但它不是登记过的共享值类型")
    return problems


def check_native_shapes(product_dir: Path) -> list[str]:
    """原生形态夹具的产物文本判据（codegen-native-binding-prereqs 任务 2.2）。

    为什么用文本判据：这些断言的是"C ABI 槽存在且形状对、托管表面逐字对"，
    不需要真的起托管运行时；tester 用例数固定 16 条，不新增用例。
    每加一条能力（2.2 / 2.3 / 2.4 / 3.x），这里就加对应的逐字行 —— 判据先红后绿。
    """
    problems: list[str] = []
    cpp_path = product_by_interface(product_dir, "IBindNativeShape", ".Bindings.cpp")
    cs_path = product_by_interface(product_dir, "IBindNativeShape", ".Bindings.cs")
    if cpp_path is None:
        problems.append("  IBindNativeShape：缺 <限定名线形>.Bindings.cpp")
        return problems
    if cs_path is None:
        problems.append("  IBindNativeShape：缺 <限定名线形>.Bindings.cs")
        return problems
    cpp = cpp_path.read_text(encoding="utf-8", errors="replace")
    cs = cs_path.read_text(encoding="utf-8", errors="replace")
    # 2.2：非纯虚 virtual / 非虚公开方法 / 自返回链式（C ABI 返回 void）。
    c_abi_expected = [
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__GetCounter(void* self);',
        'extern "C" void ca_bind_fixture__IBindNativeShape__SetCounter(void* self, uint32_t value);',
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__GetVirtualValue(void* self);',
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__GetDoubled(void* self);',
        'extern "C" void ca_bind_fixture__IBindNativeShape__ChainAdd(void* self, uint32_t value);',
    ]
    cs_expected = [
        "public uint GetCounter()",
        "public void SetCounter(uint value)",
        "public uint GetVirtualValue()",
        "public uint GetDoubled()",
        "public IBindNativeShape ChainAdd(uint value)",
    ]
    # 2.4：同名两签重载 → 第 1 槽保持现名、第 2 槽 `_2`；C# 出同名重载。
    c_abi_expected += [
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__Overloaded(void* self, uint32_t value);',
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__Overloaded_2(void* self, uint32_t value, uint32_t add);',
    ]
    cs_expected += [
        "public uint Overloaded(uint value)",
        "public uint Overloaded(uint value, uint add)",
    ]
    # 2.3：纯标量记录静态工厂 + 默认构造 Create（句柄登记 + 配对释放 + 释放后调用抛异常）。
    c_abi_expected += [
        'extern "C" NativeCountC ca_bind_fixture__IBindNativeShape__MakeTally(void* self, uint32_t value);',
    ]
    cs_expected += [
        "public NativeCount MakeTally(uint value)",
    ]
    # 3.1：资源引用值类作为参数过界（kind + 对象句柄 + 名称 ptr/len），托管表面收托管值类型。
    c_abi_expected += [
        'extern "C" void ca_bind_fixture__IBindNativeShape__ConsumeRef(void* self, uint32_t resourceRefKind, '
        'uint64_t resourceRefHandle, uint8_t const* resourceRefName, uint64_t resourceRefNameLen, '
        'uint32_t channel);',
    ]
    cs_expected += [
        "public void ConsumeRef(NativeResourceRef resourceRef, uint channel)",
    ]
    for line in c_abi_expected:
        if line not in cpp:
            problems.append(f"  IBindNativeShape：C ABI 里没有 `{line}`")
    for line in cs_expected:
        if line not in cs:
            problems.append(f"  IBindNativeShape：托管表面里没有 `{line}`")
    # 3.1：值类自身的成员方法 MUST NOT 被采集成表面（只作参数过界）。
    value_stem_used = False
    for product in sorted(product_dir.glob("*.Bindings.cpp")):
        if "NativeResourceRef_" in product.read_text(encoding="utf-8", errors="replace"):
            value_stem_used = True
            problems.append(f"  资源引用值类：{product.name} 里出现了 NativeResourceRef_ 方法槽"
                            f"（值类成员方法不应被采集）")
    value_cs_path = product_dir / "ca_bind_fixture__NativeResourceRef.Bindings.cs"
    if not value_cs_path.is_file():
        problems.append("  资源引用值类：缺 NativeResourceRef.Bindings.cs")
    else:
        value_cs = value_cs_path.read_text(encoding="utf-8", errors="replace")
        for line in (
            "public sealed class NativeResourceRef",
            "public static NativeResourceRef FromBindNativeShape(IBindNativeShape value)",
            "public static NativeResourceRef FromName(string name)",
        ):
            if line not in value_cs:
                problems.append(f"  资源引用值类：托管值类型里没有 `{line}`")
    # 3.2：序列化块记录（输入方向、块头三项与 C++ 同源；托管序列化器与镜像类型同文件）。
    c_abi_expected += [
        'extern "C" void ca_bind_fixture__IBindNativeShape__ConsumePass(void* self, uint8_t const* passBytes, '
        'uint64_t passLen);',
    ]
    cs_expected += [
        "public void ConsumePass(PassLike pass)",
    ]
    # 3.3：参数注入 —— C ABI 与托管表面都只有 token，没有 externalState。
    c_abi_expected += [
        'extern "C" void ca_bind_fixture__IBindNativeShape__SetInjectedPointer(void* self, uint32_t token);',
    ]
    cs_expected += [
        "public void SetInjectedPointer(uint token)",
    ]
    for line in c_abi_expected:
        if line not in cpp:
            problems.append(f"  IBindNativeShape：C ABI 里没有 `{line}`")
    for line in cs_expected:
        if line not in cs:
            problems.append(f"  IBindNativeShape：托管表面里没有 `{line}`")
    for name, lines in (
        ("PassLike", (
            "public sealed class PassLike",
            "internal static class PassLikeCodec",
            "public const uint FormatVersion = 1u;",
            "public const ulong TypeNameHash",
            "public const ulong LayoutHash",
            "writer.Write(1u);",
            "writer.Write(TypeNameHash);",
            "writer.Write(LayoutHash);",
            "public static byte[] Serialize(PassLike value)",
        )),
        ("PassAttachment", (
            "public struct PassAttachment",
            "public uint index;",
            "public float weight;",
        )),
    ):
        path = product_by_interface(product_dir, name, ".Bindings.cs")
        if path is None:
            problems.append(f"  序列化块记录：缺 <限定名线形>.Bindings.cs（{name}）")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for line in lines:
            if line not in text:
                problems.append(f"  序列化块记录 {name}：托管产物里没有 `{line}`")
    codec_header = product_by_interface(product_dir, "PassLike", ".BindingCodec.h")
    if codec_header is None:
        problems.append("  序列化块记录：缺 PassLike 的 <限定名线形>.BindingCodec.h（C++ 反序列化入口）")
    else:
        codec_text = codec_header.read_text(encoding="utf-8", errors="replace")
        for line in ("kTypeNameHash", "kLayoutHash", "kLayoutText", "static uint32_t Deserialize",
                     "static void Serialize"):
            if line not in codec_text:
                problems.append(f"  序列化块记录 PassLike：C++ 编解码头里没有 `{line}`")
    # ① 模板方法 MUST NOT 进产物（CA_BIND_SKIP 显式排除）；夹具头的 #include 行不算。
    for name, text in (("C ABI", cpp), ("托管表面", cs)):
        if "GetAs" in text:
            problems.append(f"  IBindNativeShape：{name} 产物里出现了模板方法 GetAs（应被显式排除）")
    # 2.3：Create 的托管行为以生成 .cs 逐字文本判据核（不新增 tester 用例）：
    #   静态 Create 工厂、句柄租约、配对释放入口、已释放对象调用抛 ObjectDisposedException。
    object_cs_path = product_dir / "ca_bind_fixture__BindNativeObject.Bindings.cs"
    if not object_cs_path.is_file():
        problems.append("  BindNativeObject：缺 BindNativeObject.Bindings.cs")
        return problems
    object_cs = object_cs_path.read_text(encoding="utf-8", errors="replace")
    object_cpp_path = product_dir / "ca_bind_fixture__BindNativeObject.Bindings.cpp"
    if not object_cpp_path.is_file():
        problems.append("  BindNativeObject：缺 BindNativeObject.Bindings.cpp")
        return problems
    object_cpp = object_cpp_path.read_text(encoding="utf-8", errors="replace")
    if 'extern "C" uint64_t ca_bind_fixture__BindNativeObject__Create(void* self);' not in object_cpp:
        problems.append(
            '  BindNativeObject：C ABI 里没有 `extern "C" uint64_t ca_bind_fixture__BindNativeObject__Create(void* self);`')
    for line in (
        "public static BindNativeObject Create()",
        "Release = BindNativeObjectBindings.ReleaseHandle",
        'if (lease.Disposed) { throw new ObjectDisposedException("BindNativeObject"); }',
    ):
        if line not in object_cs:
            problems.append(f"  BindNativeObject：托管表面里没有 `{line}`")
    # 3.3：注入钩子必须进注入片段（`<接口>.inject.part`，装配侧按这行核对；
    # 有注入没钩子在生成期已经拒绝）。
    inject_part = product_by_interface(product_dir, "IBindNativeShape", ".inject.part")
    if inject_part is None:
        problems.append("  参数注入：缺 <限定名线形>.inject.part（钩子声明没有进注入片段）")
    else:
        inject_text = inject_part.read_text(encoding="utf-8", errors="replace")
        if "# inject\tCAInject__ca_bind_fixture__IBindNativeShape__SetInjectedPointer__externalState" not in inject_text:
            problems.append("  参数注入：注入片段里没有钩子行")
    # 1.1：内存窗参数投影（design D2）—— C ABI 两段（指针 + 长度），托管表面收 `byte[]`，
    # 长度由托管算（`fixed` + `(ulong)data.Length`），裸指针不出现在托管表面。
    window_abi = ('extern "C" void ca_bind_fixture__IBindNativeShape__UploadWindow(void* self, uint8_t const* data, '
                  'uint64_t dataCount, uint64_t offset, int32_t copy);')
    if window_abi not in cpp:
        problems.append(f"  IBindNativeShape：内存窗 C ABI 不是两段（指针+长度），缺 `{window_abi}`")
    for line in (
        "public void UploadWindow(byte[] data, ulong offset, bool copy)",
        "fixed (byte* dataPtr = data)",
        "IBindNativeShapeBindings.UploadWindow(lease.Self, dataPtr, (ulong)data.Length, offset, (copy ? 1 : 0));",
    ):
        if line not in cs:
            problems.append(f"  IBindNativeShape：内存窗托管表面里没有 `{line}`")
    # 裸指针不进托管表面：这条形状的托管签名里不许出现 `IntPtr` / `void*`。
    for line in re.findall(r"public void UploadWindow\([^)]*\)", cs):
        if "IntPtr" in line or "void*" in line:
            problems.append(f"  IBindNativeShape：内存窗的托管签名把裸指针暴露出来了：`{line}`")
    # 1.2：记录两段式投影（design D8）—— C ABI 两个伴生（Len / Copy），托管表面收记录副本，
    # 空命中 `null`，托管侧不出现裸指针。
    for line in (
        'extern "C" uint64_t ca_bind_fixture__IBindNativeShape__FindPassLen(void* self, char const* passName, '
        'uint64_t passNameLen);',
        'extern "C" void ca_bind_fixture__IBindNativeShape__CopyFindPass(void* self, char const* passName, '
        'uint64_t passNameLen, uint8_t* dst, uint64_t cap);',
    ):
        if line not in cpp:
            problems.append(f"  IBindNativeShape：记录两段式的 C ABI 里没有 `{line}`")
    for line in (
        "public PassLike FindPass(string passName)",
        "ulong length = IBindNativeShapeBindings.FindPassLen(lease.Self, passNamePtr, (ulong)passNameBytes.Length);",
        "if (length == 0UL) { return null; }",
        "return PassLikeCodec.Deserialize(buffer);",
    ):
        if line not in cs:
            problems.append(f"  IBindNativeShape：记录两段式的托管表面里没有 `{line}`")
    for line in re.findall(r"public [^;\n]* FindPass\([^)]*\)", cs):
        if "IntPtr" in line or "void*" in line:
            problems.append(f"  IBindNativeShape：记录两段式的托管签名把裸指针暴露出来了：`{line}`")
    # 1.3：模板显式实例化（design D9）—— 两个声明形态各出具体槽 + 托管同名重载；
    # 未声明的形态（double）在任何表面里 MUST 0 命中。
    for line in (
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__TemplatedValue_uint32_t(void* self, uint32_t value);',
        'extern "C" uint32_t ca_bind_fixture__IBindNativeShape__TemplatedValue_float(void* self, float value);',
    ):
        if line not in cpp:
            problems.append(f"  IBindNativeShape：模板实例化的 C ABI 里没有 `{line}`")
    for line in (
        "public uint TemplatedValue(uint value)",
        "public uint TemplatedValue(float value)",
    ):
        if line not in cs:
            problems.append(f"  IBindNativeShape：模板实例化的托管表面里没有 `{line}`")
    if "TemplatedValue_double" in cpp or "TemplatedValue(double" in cs:
        problems.append("  IBindNativeShape：未声明的模板形态 double 进了表面（D9 只允许声明过的形态）")
    # 1.5：图形状序列化（design D11）—— 三类元素进序列化块：值类容器 / 命名映射 / 接口对象引用。
    graph_abi = ('extern "C" void ca_bind_fixture__IBindNativeShape__ConsumeGraphShape(void* self, '
                 'uint8_t const* shapeBytes, uint64_t shapeLen);')
    if graph_abi not in cpp:
        problems.append(f"  IBindNativeShape：图形状的 C ABI 里没有 `{graph_abi}`")
    if "public void ConsumeGraphShape(GraphLikeRecord shape)" not in cs:
        problems.append("  IBindNativeShape：图形状的托管表面里没有 `public void ConsumeGraphShape(GraphLikeRecord shape)`")
    graph_path = product_by_interface(product_dir, "GraphLikeRecord", ".Bindings.cs")
    if graph_path is None:
        problems.append("  图形状序列化：缺 GraphLikeRecord 的 <限定名线形>.Bindings.cs")
    else:
        graph_cs = graph_path.read_text(encoding="utf-8", errors="replace")
        for line in (
            "public NativeResourceRef[] m_Refs;",
            "public System.Collections.Generic.Dictionary<string, uint> m_Counts;",
            "public System.Collections.Generic.Dictionary<string, PassAttachment> m_Attachments;",
            "public System.Collections.Generic.Dictionary<string, NativeResourceRef> m_RefsByName;",
            "public IBindNativeShape m_Object;",
            "public System.Collections.Generic.Dictionary<string, IBindNativeShape> m_Objects;",
            # ① 值类元素的写线形；② 值类的三段线形；③ 接口引用按句柄
            # （局部名带字段后缀 —— 嵌套记录三层时 C# 不许同名局部变量重复声明）
            "writer.Write(item_m_Refs.Kind);",
            "writer.Write(pair_m_RefsByName.Value.Kind);",
            "writer.Write((ulong)(pair_m_Objects.Value == null ? 0UL : (ulong)pair_m_Objects.Value.Token));",
            "writer.Write((ulong)(value.m_Object == null ? 0UL : (ulong)value.m_Object.Token));",
        ):
            if line not in graph_cs:
                problems.append(f"  图形状序列化：GraphLikeRecord.Bindings.cs 里没有 `{line}`")
    graph_codec = product_by_interface(product_dir, "GraphLikeRecord", ".BindingCodec.h")
    if graph_codec is None:
        problems.append("  图形状序列化：缺 GraphLikeRecord 的 <限定名线形>.BindingCodec.h（C++ 反序列化入口）")
    else:
        graph_cpp = graph_codec.read_text(encoding="utf-8", errors="replace")
        for line in ("BindingHandleResolver resolver", "reader.resolver.resolve", "return 12u;"):
            if line not in graph_cpp:
                problems.append(f"  图形状序列化：C++ 编解码头里没有 `{line}`")
        if "BAD_OBJECT_HANDLE" not in cpp:
            problems.append("  图形状序列化：产物里没有 ERROR_BAD_OBJECT_HANDLE（查无句柄的错误码 1011）")
    # 1.6：返回位置标量容器两段式（D13）—— Count + Copy 两个伴生 + 托管数组表面。
    count_abi = 'extern "C" uint64_t ca_bind_fixture__IBindNativeShape__GetPassIndicesCount(void* self);'
    copy_abi = ('extern "C" void ca_bind_fixture__IBindNativeShape__CopyGetPassIndices(void* self, uint8_t* dst, '
                'uint64_t cap);')
    for line in (count_abi, copy_abi):
        if line not in cpp:
            problems.append(f"  IBindNativeShape：标量容器两段式的 C ABI 里没有 `{line}`")
    if "public uint[] GetPassIndices()" not in cs:
        problems.append("  IBindNativeShape：标量容器两段式的托管表面里没有 `public uint[] GetPassIndices()`")
    for line in ("ulong count = IBindNativeShapeBindings.GetPassIndicesCount(lease.Self);",
                 "return new uint[0];",
                 "(ulong)values.Length * 4UL",
                 "return values;"):
        if line not in cs:
            problems.append(f"  IBindNativeShape：标量容器两段式的托管表面里没有 `{line}`")
    return problems


def check_foundation_surface(product_dir: Path) -> list[str]:
    """`IBindFoundation` 的公开表面签名（任务 1.1）。

    这一层断言的是**具体签名整行**，不是方法名：`bool` 返回成 `int`、`GetFlags` 的参数
    还成 `ushort`、包装值还成 `IntPtr`、`byte[]` 还成 `byte*` 都能保持方法名不变，
    只有逐字签名才判得出来。记录 / 枚举的字段内容与真实 CLR 布局由
    `check_shared_value_types` 与托管启动检查负责。
    """
    path = product_dir / "ca_bind_fixture__IBindFoundation.Bindings.cs"
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
# 夹具识别：`BindableShapes*.h` 里的 `CA_BIND(编号)` 或 `CA_BIND()` 都要认出来 —— 只认空括号
# 会把带号夹具（render-interface-merge 任务 1.1 的编号 8/9）当成真实接口，桩判据立刻红。
CA_BIND_CLASS_RE = re.compile(r"CA_BIND\s*\([^)]*\)\s*class\s+(\w+)", re.S)


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


def split_linear(name: str) -> tuple[str, str]:
    """`graphics_backend__GPUGraph` → (`graphics_backend`, `GPUGraph`)；无 `__` 时命名空间为空。"""
    namespace, sep, short = name.rpartition("__")
    return (namespace, short) if sep else ("", name)


def python_linear(qualified: str) -> str:
    """与生成器 qualifiedLinearName 同规则的 Python 版：`a::b::Widget` → `a__b__Widget`。"""
    split = qualified.rfind("::")
    if split == -1:
        return qualified
    return qualified[:split].replace("::", "__") + "__" + qualified[split + 2:]


def csharp_constant_suffix(qualified: str) -> str:
    """与生成器 interfaceConstantSuffix 同规则：PascalCase 命名空间段 + `__` + 短名去 I + InterfaceId。"""
    parts = qualified.split("::")
    short = parts[-1]
    bare = short[1:] if len(short) > 1 and short[0] == "I" and short[1].isupper() else short
    segments = []
    for part in parts[:-1]:
        segments.append("".join(word[:1].upper() + word[1:].lower()
                                for word in part.split("_")))
    namespace = "__".join(segments)
    return (namespace + "__" if namespace else "") + bare + "InterfaceId"


def interface_of(path: Path) -> str:
    """产物文件名是限定名线形（本批 D6），接口身份取末尾短名。"""
    linear = path.name[: -len(".Bindings.cpp")]
    return split_linear(linear)[1]


def product_by_interface(product_dir: Path, interface: str, suffix: str) -> Path | None:
    """按短名找逐接口产物（文件名是限定名线形）。唯一命中才返回。"""
    matches = [path for path in sorted(product_dir.glob(f"*{suffix}"))
               if split_linear(path.name[: -len(suffix)])[1] == interface]
    return matches[0] if len(matches) == 1 else None


def product_linear_name(product_dir: Path, interface: str, suffix: str) -> str:
    path = product_by_interface(product_dir, interface, suffix)
    return path.name[: -len(suffix)] if path is not None else interface


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


def is_self_return(interface: str, declared: str) -> bool:
    """`declarations` 里的返回拼写是不是"该接口自身的引用"（任务 2.2 的丢返回值豁免）。

    识别口径（写死，不放宽）：末尾必须是 `&`；去掉末尾 `const` 后的短名必须等于接口名。
    因此 `IBindNativeShape&` / `IBindNativeShape const&` 豁免；
    `castl::shared_ptr<BindScalarThing>` / `uint32_t` 这类非自身引用仍然按"丢返回值"拒绝。
    """
    text = " ".join(declared.split())
    if not text.endswith("&"):
        return False
    core = text[:-1].strip()
    if core.endswith("const"):
        core = core[:-len("const")].strip()
    return core.rsplit("::", 1)[-1] == interface


def self_test_self_return_exemption() -> int:
    """豁免判据的自检（任务 2.2 的"防豁免过界"）。

    为什么要有它：夹具里唯一的"投影成 void 但声明不是 void"的方法就是自返回链式方法，
    没有第二个真实夹具能直接证明"把豁免写宽之后会漏掉非自身引用的丢返回值"。这里用
    `is_self_return` 的两组输入做自检：写宽成"任何返回值都豁免"时，`IBindRow09` 的
    `castl::shared_ptr<BindScalarThing>` 会被误判为豁免 ⇒ 自检立刻红。
    """
    cases = [
        ("IBindNativeShape", "IBindNativeShape&", True),
        ("IBindNativeShape", "IBindNativeShape const&", True),
        ("IBindRow09", "castl::shared_ptr<BindScalarThing>", False),
        ("IBindRow09", "uint32_t", False),
    ]
    problems: list[str] = []
    for interface, declared, expected in cases:
        actual = is_self_return(interface, declared)
        if actual != expected:
            problems.append(
                f"  自返回豁免自检：{interface} + `{declared}` 期望 "
                f"{'豁免' if expected else '不豁免'}，实际 {'豁免' if actual else '不豁免'}")
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f"自返回豁免自检：{len(cases)} 例 / 问题 {len(problems)} 处")
    return 1 if problems else 0


def check_stubs(product_dir: Path) -> tuple[int, int, int, list[str], list[str], list[str], list[str]]:
    """返回（真实接口数, 夹具接口数, 夹具桩数, 真实接口问题, 丢返回值问题, 结构问题, 夹具丢返回值问题）。"""
    fixtures = fixture_interface_names()
    products = [path for path in sorted(product_dir.glob("*.Bindings.cpp"))
                if not is_registry_product(path)]
    # 反向产物（C# 实现 C++）没有 thunk / Api_v2 方法槽：它走 check_reverse_stubs 的替代判据
    # （代理方法体非空 + 析构调 releaseUser），不进"正向 thunk 桩"这条判据（design D9）。
    products = [path for path in products if not is_reverse_product(path)]
    real_paths = [path for path in products if interface_of(path) not in fixtures]
    real_names = {interface_of(path) for path in real_paths}

    real_problems: list[str] = []
    dropped_problems: list[str] = []
    fixture_dropped: list[str] = []
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
                # 两段式伴生的两个槽（`<方法>Len` / `Copy<方法>`，以及本批 1.6/D13 的
                # `<方法>Count` / `Copy<方法>`）没有 thunk —— 它们的实现体直接调接口方法。
                # 按命名认它们；"真方法漏了 thunk"由 check_coverage 对着期望文件抓。
                is_two_step = (member.endswith("Len") or member.startswith("Copy")
                               or member.endswith("Count"))
                if is_two_step:
                    continue
                structure_problems.append(
                    f"  {interface}::{member}：函数表里有这个槽，产物里却没有对应的 thunk")
                continue
            projected, body = thunks[member]
            projected_void = projected == "void"
            # `Create` 是工厂槽：它的 thunk 就是 `new <类>()`（没有 `object->` 调用），
            # 按"桩"判会误报（实测 `GPUGraph::Create：thunk 体内没有真正调用实现（桩）`）。
            if thunk_is_stub(body, projected_void) and member != "Create":
                if is_real:
                    real_problems.append(f"  {interface}::{member}：thunk 体内没有真正调用实现（桩）")
                else:
                    fixture_stubs += 1
                continue
            # 独立红条件（design D2 第 4 条）：投影成 void，而实现返回的不是 void ⇒ 调了但丢结果
            if projected_void and member in declarations and declarations[member] != "void":
                if is_self_return(interface, declarations[member]):
                    # 返回本类引用的链式方法：C ABI 就是 void（托管侧返回 this），不算丢返回值。
                    continue
                if is_real:
                    dropped_problems.append(
                        f"  {interface}::{member}：投影成 void，但实现返回 {declarations[member]}"
                        f" —— 调用了却把返回值丢掉")
                else:
                    # 夹具也判（任务 2.2）：自返回链式方法在豁免规则生效前会落进这里；豁免实现后 0 处。
                    fixture_dropped.append(
                        f"  {interface}::{member}：投影成 void，但实现返回 {declarations[member]}"
                        f" —— 调用了却把返回值丢掉（夹具）")

    return len(real_names), len(products) - len(real_names), fixture_stubs, \
        real_problems, dropped_problems, structure_problems, fixture_dropped


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
            # 新槽名是 `<限定名线形>__<方法>`，方法名内部不含 `__`，所以从最后一个 `__` 切。
            short = symbol.rsplit("__", 1)[-1]
            if is_companion(short):
                continue
            expected_by_interface.setdefault(row, set()).add(symbol)

    for interface, expected_names in sorted(expected_by_interface.items()):
        path = product_by_interface(product_dir, interface, ".Bindings.cpp")
        if path is None:
            problems.append(f"  {interface}：期望文件里列了 {len(expected_names)} 个方法，但产物文件不存在")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        linear = path.name[: -len(".Bindings.cpp")]
        actual_names = {f"{linear}__{method}" for method in extract_thunks(text)}
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
    # 两段式的基础方法名：`<线形>__<M>Len` 存在 ⇒ `M` / `MLen` / `CopyM` 三个都算伴生，不进方法集合
    two_step_bases: dict[str, set[str]] = {}
    for symbol in symbols:
        match = re.fullmatch(r"(.+)__(.+)Len", symbol)
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
        if name.startswith("Release") and f"{interface}__{name[7:]}" in symbols:
            return True
        return False

    for path in sorted(product_dir.glob("*.Bindings.cpp")):
        if is_registry_product(path):
            continue
        # 反向产物（类别标记）：函数表是反向 glue 表，没有正向 thunk/槽对齐 —— 由
        # check_reverse_alignment 判"代理 override ↔ C# abstract"（design D9 的替代判据）。
        if is_reverse_product(path):
            continue
        linear = path.name[: -len(".Bindings.cpp")]
        prefix = linear + "__"
        text = path.read_text(encoding="utf-8", errors="replace")
        for declaration in DECL_RE.findall(text):
            match = SYMBOL_RE.search(declaration)
            if match is None:
                continue
            symbol = match.group(1)
            # 注入钩子（`CAInject__<线形>__<方法>__<参数>`）不是接口槽：extern "C" 声明进产物只为
            # thunk 可见性，不参与"C++ 槽 ↔ C# 字段"的两侧对齐。
            if symbol.startswith("CAInject__") or not symbol.startswith(prefix):
                continue
            method = symbol[len(prefix):]
            if is_generic(linear, method):
                continue
            cpp_pairs.setdefault(linear, []).append(method)
    for path in sorted(product_dir.glob("*.Bindings.cs")):
        interface = path.name[: -len(".Bindings.cs")]
        text = path.read_text(encoding="utf-8", errors="replace")
        if is_reverse_product(path):
            continue  # 反向产物的两侧对齐在 check_reverse_alignment 里判
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
                                        for path in product_dir.glob("*.Bindings.cpp")
                                        if not is_registry_product(path)
                                        and not is_reverse_product(path)}
    for interface in sorted(interface_stems):
        short = split_linear(interface)[1]
        if short in fixtures:
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
        if short in NATIVE_INTERFACES:
            # 原生四类的函数表名带重载后缀（`SetImage_2`）与实例化形态（`SetValue_float3`）：
            # 都归一到表面里的同名成员再比**多重集**（重载/实例化的顺序不是契约，个数是）。
            base_names = sorted(set(members), key=len, reverse=True)
            normalized = []
            seen_two_step: set[str] = set()
            for name in expected:
                # 两段式伴生槽（`<方法>Len` / `<方法>Count` / `Copy<方法>`）与主方法合成**一个**成员：
                # 它们在 C# 表面就是同一个方法（`GetPassIndices()` / `GetName()` 等）。
                two_step_base = None
                if name.endswith("Len") and name[:-3] in base_names:
                    two_step_base = name[:-3]
                elif name.endswith("Count") and name[:-5] in base_names:
                    two_step_base = name[:-5]
                elif name.startswith("Copy") and name[4:] in base_names:
                    two_step_base = name[4:]
                if two_step_base is not None:
                    if two_step_base in seen_two_step:
                        continue
                    seen_two_step.add(two_step_base)
                base = two_step_base or next(
                    (candidate for candidate in base_names
                     if name == candidate or name.startswith(candidate + "_")), name)
                normalized.append(base)
            if sorted(normalized) == sorted(members):
                continue
            only_expected = [name for name in normalized if name not in members]
            only_actual = [name for name in members if name not in normalized]
            problems.append(f"  {interface}（表面层）：C++ 多 {only_expected or '无'}，"
                            f"C# 表面多 {only_actual or '无'}（归一后 C++ {sorted(normalized)} / "
                            f"C# {sorted(members)}）")
            continue
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


HANDLE_NAME_RE = re.compile(r"(?i)(handle|token|lease|self|registration)$")
RETURN_ULONG_RE = re.compile(r"\bpublic\b[^()]*\bulong\s+\w+\s*\(")


def public_ulong_problems(interface: str, block: dict,
                          scalar_uint64_params: set[str] | None = None) -> list[str]:
    """public 类型体里的 `ulong` 判据（口径细化：只挡句柄，不挡真标量）。

    原口径是"public 类型体里出现 `ulong` 即失败"（句柄必须包成表面对象）。绑定面换到原生接口后，
    `uint64_t` 的真标量参数（`GPUGraph::ScheduleData` 的 offset、`CreateGPUBuffer` 的长度）
    也会投影成 `ulong` —— 一刀切会把合法标量判红。句柄在生成物里有固定命名：参数叫
    `<名>Handle`（弱/强/借用解析出来的）或 `<名>Token`，租约字段叫 `<X>Lease`，
    返回值位置直接是句柄。这里按这两个位置判，句柄仍然过不去。
    `scalar_uint64_params`：接口头里**字面拼写就是 `uint64_t` 的参数名**集合 —— 这些参数是显式标量
    转移（本批 `IResourceRegistry::RegisterFactory` 的反向工厂强句柄就是这种），不是被投影层包成
    `ulong` 的原生句柄；它们的 C++ 形态不是 `shared_ptr`，没有"句柄泄漏成标量"的问题。
    """
    problems: list[str] = []
    allowed = scalar_uint64_params or set()
    for line in block["body"].splitlines():
        if "ulong" not in line:
            continue
        stripped = line.strip()
        if RETURN_ULONG_RE.search(stripped):
            problems.append(f"  {interface}：public 类型 `{block['name']}` 的返回值里出现了 `ulong`（句柄）")
            continue
        for name in re.findall(r"\bulong\s+(\w+)", stripped):
            if HANDLE_NAME_RE.search(name) and name not in allowed:
                problems.append(
                    f"  {interface}：public 类型 `{block['name']}` 的参数 `{name}` 是 `ulong`（句柄形态）")
    return problems


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
        linear = path.name[: -len(".Bindings.cs")]
        interface = split_linear(linear)[1]
        # 共享公开值类型（同一次生成里按类型另发的文件）不是接口：它们没有 `.Bindings.cpp`，
        # 也不该有 `IDisposable` 表面类。内容与唯一性由 check_shared_value_types 判。
        if not (product_dir / f"{linear}.Bindings.cpp").is_file():
            continue
        # 反向产物（C# 实现 C++）改判 check_reverse_base：公开抽象基类干净 + abstract 数 = 被绑定虚方法数
        # （design D9）。正向判据（IDisposable 表面类 / `<I>Exception` / 表面成员数）对反向不适用，
        # 但正向产物本身的判据一个字没动 —— 这是替代，不是放松。
        if is_reverse_product(path):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        # 表面 `ulong` 判据的标量豁免（csharp-resource-asset-foundation）：接口头里字面写成
        # `uint64_t <名>` 的参数是显式标量转移，不是投影层发出来的原生句柄。只在**该接口自己的头**
        # 里找，不跨接口放行；句柄形态（`shared_ptr` 参数）没有这个字面拼写，照旧判红。
        scalar_uint64_params: set[str] = set()
        product_for_header = path.with_name(f"{linear}.Bindings.cpp")
        if product_for_header.is_file():
            header_for_exemption = resolve_interface_header(
                product_for_header.read_text(encoding="utf-8", errors="replace"))
            if header_for_exemption is not None:
                scalar_uint64_params = set(re.findall(
                    r"\buint64_t\s+(\w+)\s*[,)]",
                    strip_comments(header_for_exemption.read_text(encoding="utf-8", errors="replace"))))
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
            problems.extend(public_ulong_problems(interface, block, scalar_uint64_params))
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
        product = path.with_name(f"{linear}.Bindings.cpp")
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
        if interface in NATIVE_INTERFACES:
            # 原生四类（csharp-native-direct-binding 2.2）：方法名会被重载后缀（`SetImage_2`）和
            # 实例化形态（`SetValue_float3` / `SetValue_float`）加长，表面里则是同名重载 ⇒
            # 计条数的旧公式不适用。这里判**名字级**：表面里出现的每个方法名都必须能在头里找到
            # 同名公开声明（模板方法按 `CA_BIND_INSTANTIATE` 声明算一个名字）；头里声明的名字，
            # 除被 `CA_BIND_SKIP` 排除的以外，都必须在表面里出现。
            names = native_header_method_names(interface, body)
            # `CA_BIND_SKIP` 写在类体之后（本批的接口头都这么写）：从**整份头文本**取名字。
            skipped = set(re.findall(r"CA_BIND_SKIP\s*\(\s*(\w+)\s*,",
                                     header.read_text(encoding="utf-8", errors="replace")))
            # `Create` 是生成器给"带公开默认构造的非抽象类"发的工厂槽（csharp-bindings 主规范
            # 要求），不是头里的方法：名字级判据放行它。
            extra = sorted({name for name in members if name not in names and name != "Create"})
            missing = sorted({name for name in names if name not in members and name not in skipped})
            if extra:
                problems.append(f"  {interface}：托管表面出现头里没有的方法 {extra}")
            if missing:
                problems.append(f"  {interface}：头里声明的方法没进托管表面 {missing}")
            continue
        if len(members) != expected:
            problems.append(f"  {interface}：表面成员 {len(members)} 个，期望 {expected} 个"
                            f"（纯虚 {len(VIRTUAL_RE.findall(body))} 条 − 排除 {len(SKIP_RE.findall(body))} 条）")
    return problems


NATIVE_INTERFACES = {"CRenderBackend", "GPUGraph", "ShaderStruct", "GPUTexture"}
NATIVE_METHOD_NAME_RE = re.compile(r"(\w+)\s*\(")


def native_header_method_names(interface: str, body: str) -> set[str]:
    """原生接口头里的公开方法名集合（含带默认参数的通道构造之外的普通方法；模板方法按
    `CA_BIND_INSTANTIATE` 声明算一个名字）。解析口径与 `check_bindable_audit.py` 的
    `header_methods` 同源：按访问标号切段，只留 public 段。这里只回集合，用于"表面方法名能否
    在头里找到"的判据。"""
    inner = body.strip()
    if inner.startswith("{"):
        inner = inner[1:]
    if inner.endswith("}"):
        inner = inner[:-1]
    inner = re.sub(r"\{[^{}]*\}", ";", inner)
    names: set[str] = set()
    visibility = "private"
    for segment in re.split(r"\b(public|private|protected)\s*:", inner):
        if segment in ("public", "private", "protected"):
            visibility = segment
            continue
        if visibility != "public":
            continue
        for decl in segment.split(";"):
            text = decl.strip()
            if not text or "(" not in text:
                continue
            if text.startswith(("static_assert", "CA_", "friend", "using", "typedef")):
                continue
            if text.startswith("template"):
                rest = text[text.find(">") + 1:] if ">" in text else ""
                match = NATIVE_METHOD_NAME_RE.search(rest)
                if match:
                    names.add(match.group(1))
                continue
            match = NATIVE_METHOD_NAME_RE.search(text)
            if not match:
                continue
            name = match.group(1)
            if name.startswith("~") or name == interface:
                continue
            names.add(name)
    return names


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
        path = product_by_interface(product_dir, interface, ".Bindings.cs")
        if path is None:
            problems.append(f"  第二张矩阵 {row} 行（{description}）：缺 {interface}.Bindings.cs")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if signature not in text:
            problems.append(f"  第二张矩阵 {row} 行（{description}）：{interface} 生成的表面里没有 "
                            f"`{signature}` —— 映射写错了")
    return problems


ERROR_CODE_TABLE = {1001: "STALE_HANDLE", 1002: "NULL_POINTER",
                    1003: "CALLBACK_EXCEPTION", 1004: "CALLBACK_REENTRANT",
                    1005: "CALLBACK_WRONG_THREAD", 1006: "INVALID_MONITOR",
                    # codegen-native-binding-prereqs 任务 3.2：序列化块四项
                    1007: "BAD_BLOCK_VERSION", 1008: "BAD_BLOCK_TYPE",
                    1009: "BAD_BLOCK_LAYOUT", 1010: "BAD_BLOCK_PAYLOAD",
                    # csharp-native-direct-binding 2.2：序列化块里的对象句柄查不到
                    1011: "BAD_OBJECT_HANDLE",
                    # csharp-reverse-interface-binding：反向实现抛异常 / 不支持的反向读。
                    # 三处同步：CodeGenMain.cpp 的 kBindErrorCodes / 这里 / README 的错误码表。
                    1012: "REVERSE_EXCEPTION", 1013: "REVERSE_READ_UNSUPPORTED",
                    # csharp-resource-asset-foundation 1.7：宿主门面专用（门面实现自行 catch 后写入；
                    # 只发射进 `cs_script__IResourceRegistry` 产物，既有正向产物不含它）。三处同步同上。
                    1014: "RESOURCE_OPERATION_REJECTED"}
ERROR_CONST_RE = re.compile(r"static const int32_t (\w+?)_ERROR_(\w+) = (\d+);")
SET_ERROR_RE = re.compile(r"\w+_setError\(\s*[^,]+,\s*([A-Za-z_]\w*|\d+)\s*,")

def check_error_codes(product_dir: Path) -> list[str]:
    """错误码判据（任务 5.8）：生成器发出的每个码都必须在码表里，且不许是裸字面量。

    为什么不能只查表：`setError(…, 1002, …)` 这种裸字面量不点名字，改一个码就漏一处；
    反过来只看常量也不行 —— 常量定义了却没人用，或者用了码表外的数，都判不出来。
    """
    problems: list[str] = []
    for path in sorted(product_dir.glob("*.Bindings.cpp")):
        if is_registry_product(path):
            continue
        interface = split_linear(path.name[: -len(".Bindings.cpp")])[1]
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
            # 生成出来的错误常量是 `<线形>_ERROR_<码名>`：名字里带 `_ERROR_` 却不在本产物常量表里的，
            # 就是用了没定义的常量。函数里的局部量不含 `_ERROR_`，不会被误报。
            elif "_ERROR_" in argument and argument not in constants:
                problems.append(f"  {interface}：`setError` 用了没定义的常量 {argument}")
    return problems


REGISTRY_CONST_RE = re.compile(
    r"inline constexpr uint32_t kBindingInterfaceId(\w+) = (\d+)u;\s*//\s*(\S+)")
ENGINE_API_STRUCT_RE = re.compile(r"struct EngineApi\s*\{(.*?)\};", re.S)
ENGINE_API_MIRROR_RE = re.compile(r"struct EngineApiMirror\s*\{(.*?)\}", re.S)
CPP_FIELD_NAME_RE = re.compile(r"\(\s*\*\s*(\w+)\s*\)|^\s*[\w:*]+\s+(\w+)\s*;", re.M)


def check_registry(product_dir: Path, expected_numbers: dict[int, str],
                   require_engine_api_ids: bool, contiguous_from: int | None) -> list[str]:
    """登记表产物判据（render-interface-merge 任务 1.3）。

    四类断言：
      ① 编号对账：生成物里的「编号 → 接口完整限定名」必须与期望表逐项相同（缺 / 多 / 改号 /
         换限定名都报，并点名差异）；
      ② `EngineApi` 形状：C++ struct 恰好 9 个字段、顺序与 `ENGINE_API_FIELD_PAIRS` 相同、
         `static_assert(sizeof(EngineApi) == 56u`、`kEngineApiVersion = 12`、`kEngineApiSize = 56`；
      ③ 同一次生成的托管对照：`EngineApiMirror` 恰好 9 个字段、顺序与 C++ 侧逐字段对上，
         `AbiVersion = 12`、`Size = 56`、逐接口编号常量与 ① 一致；
      ④ （主登记表）装配代码里 7 个编号字段必须引用生成常量，不许回落到 0。
    """
    problems: list[str] = []
    header_path = product_dir / "BindingRegistry.Bindings.h"
    cpp_path = product_dir / "BindingRegistry.Bindings.cpp"
    cs_path = product_dir / "EngineApi.Bindings.cs"
    for path in (header_path, cpp_path, cs_path):
        if not path.is_file():
            problems.append(f"  登记表：缺产物 {path}")
    if problems:
        return problems
    header = header_path.read_text(encoding="utf-8", errors="replace")
    cpp = cpp_path.read_text(encoding="utf-8", errors="replace")
    cs = cs_path.read_text(encoding="utf-8", errors="replace")

    # ① 编号对账
    actual: dict[int, str] = {}
    for _suffix, number, qualified in REGISTRY_CONST_RE.findall(header):
        actual[int(number)] = qualified
    for number in sorted(set(expected_numbers) | set(actual)):
        want = expected_numbers.get(number)
        got = actual.get(number)
        if want is None:
            problems.append(f"  登记表：多出编号 {number} → {got}（期望表里没有这一项）")
        elif got is None:
            problems.append(f"  登记表：缺少编号 {number} → {want}")
        elif got != want:
            problems.append(f"  登记表：编号 {number} 的接口对不上：期望 {want}，产物是 {got}")

    # 编号连续性（spec："只许在现有最大编号之后追加新号"）：这条**不依赖期望表本身** ——
    # 即使有人把期望表改成 17，越界号 / 跳号也必须红。主登记表要求恰为 1..N 连续；
    # 夹具登记表要求从 8 起连续（8..M）。
    numbers_sorted = sorted(actual)
    if numbers_sorted:
        if contiguous_from is not None and numbers_sorted[0] != contiguous_from:
            problems.append(
                f"  登记表：编号起点必须是 {contiguous_from}，实际是 {numbers_sorted[0]}"
                f"（编号清单 {numbers_sorted}）")
        expected_sequence = list(range(numbers_sorted[0], numbers_sorted[0] + len(numbers_sorted)))
        if numbers_sorted != expected_sequence:
            problems.append(
                f"  登记表：编号必须是连续区间，实际 {numbers_sorted}"
                f"（缺 {sorted(set(expected_sequence) - set(numbers_sorted))}、"
                f"多 {sorted(set(numbers_sorted) - set(expected_sequence))}）")
    else:
        problems.append("  登记表：一个编号都没读到")

    # ② C++ EngineApi 形状
    if re.search(r"inline constexpr uint32_t kEngineApiVersion = 12u;", header) is None:
        problems.append("  登记表：kEngineApiVersion 不是 11u")
    if re.search(r"inline constexpr uint32_t kEngineApiSize = 56u;", header) is None:
        problems.append("  登记表：kEngineApiSize 不是 56u")
    if "static_assert(sizeof(EngineApi) == 56u" not in header:
        problems.append("  登记表：缺 `static_assert(sizeof(EngineApi) == 56u`")
    struct_match = ENGINE_API_STRUCT_RE.search(header)
    if struct_match is None:
        problems.append("  登记表：找不到 `struct EngineApi { ... };`")
        cpp_fields: list[str] = []
    else:
        cpp_fields = []
        for function_name, variable_name in CPP_FIELD_NAME_RE.findall(struct_match.group(1)):
            cpp_fields.append(function_name or variable_name)
        expected_cpp = [cpp for cpp, _cs in ENGINE_API_FIELD_PAIRS]
        if cpp_fields != expected_cpp:
            problems.append(f"  登记表：C++ EngineApi 字段 {cpp_fields} 与期望 9 字段 {expected_cpp} 不一致")
        if len(cpp_fields) != 9:
            problems.append(f"  登记表：C++ EngineApi 字段数是 {len(cpp_fields)}，期望 9")
        for declaration in ENGINE_API_FIELD_DECLARATIONS:
            if ("    " + declaration) not in header:
                problems.append(f"  登记表：EngineApi 缺少逐字声明 `{declaration}`")

    # ③ 托管对照
    if "public const uint AbiVersion = 12;" not in cs:
        problems.append("  登记表：C# 侧 AbiVersion 不是 11")
    if "public const uint Size = 56;" not in cs:
        problems.append("  登记表：C# 侧 Size 不是 56")
    mirror_match = ENGINE_API_MIRROR_RE.search(cs)
    if mirror_match is None:
        problems.append("  登记表：找不到 `struct EngineApiMirror { ... }`")
        cs_fields: list[str] = []
    else:
        cs_fields = re.findall(r"^\s*public\s+[^;=]+\s+(\w+)\s*;", mirror_match.group(1), re.M)
        expected_cs = [cs for _cpp, cs in ENGINE_API_FIELD_PAIRS]
        if cs_fields != expected_cs:
            problems.append(f"  登记表：C# EngineApiMirror 字段 {cs_fields} 与期望 9 字段 {expected_cs} 不一致")
        if len(cs_fields) != 9:
            problems.append(f"  登记表：C# EngineApiMirror 字段数是 {len(cs_fields)}，期望 9")
    if "public const uint TableVersion = 2;" not in cs:
        problems.append("  登记表：C# 侧 TableVersion 不是 2（生成接口表版本必须保持 2）")
    for number, qualified in sorted(expected_numbers.items()):
        suffix = csharp_constant_suffix(qualified)
        if f"public const uint {suffix} = {number};" not in cs:
            problems.append(f"  登记表：C# 侧缺编号常量 {suffix} = {number}（{qualified}）")
    table_entries = re.findall(r"new InterfaceTable\(", cs)
    if len(table_entries) != len(expected_numbers):
        problems.append(f"  登记表：C# 绑定清单条目 {len(table_entries)} 个，期望 {len(expected_numbers)} 个")

    # ④ 主登记表：EngineApi 的编号字段必须装配到生成常量（不是 0）。
    # 6.2 的 ABI v10：桥五类的五个编号字段已从 struct 删除（布局 14 字段 / 80 字节 → 9 字段 / 56 字节），
    # 现在只剩两个窗口字段需要配对生成常量。
    if require_engine_api_ids:
        assembly = [
            "api.window_interface_id = kBindingInterfaceIdcawindow__IWindow;",
            "api.window_system_interface_id = kBindingInterfaceIdcawindow__IWindowSystem;",
        ]
        for line in assembly:
            if line not in cpp:
                problems.append(f"  登记表：装配代码里没有 `{line}`")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="对拍绑定产物与期望签名")
    parser.add_argument("--product-dir", required=True,
                        help="生成物目录（<构建目录>/codegen/bindings）；必填，判据只认这一个来源")
    parser.add_argument("--self-test-exemption", action="store_true",
                        help="只跑自返回豁免判据的自检（不读产物）")
    parser.add_argument("--only", default=None,
                        help="只跑一条检查（反向产物的红绿要能独立跑）："
                             "reverse_base / reverse_alignment / reverse_stubs / surface / alignment / "
                             "stubs / shared_value_types / error_codes / registry / coverage")
    args = parser.parse_args()
    if args.self_test_exemption:
        return self_test_self_return_exemption()
    product_dir = Path(args.product_dir)
    if not product_dir.is_absolute():
        product_dir = (Path.cwd() / product_dir)
    product_dir = product_dir.resolve()

    if not FIXTURE_HEADER.is_file():
        print(f"找不到夹具头：{FIXTURE_HEADER} —— 判据自身失效", file=sys.stderr)
        return 2
    if not LOCK_FILE.is_file():
        print(f"找不到编号锁文件：{LOCK_FILE} —— 判据自身失效（编号唯一来源是锁文件）", file=sys.stderr)
        return 2
    try:
        host_numbers = read_lock_scope("host")
        fixture_numbers = read_lock_scope("fixture")
    except ValueError as error:
        print(f"{error} —— 判据自身失效", file=sys.stderr)
        return 2
    if not product_dir.is_dir():
        print(f"产物目录不存在：{product_dir} —— 判据自身失效"
              f"（这个退出码 2 与「发现桩」的退出码 1 是分开的）", file=sys.stderr)
        return 2

    # ---- `--only <检查名>`（csharp-reverse-interface-binding 2.2）：单检查入口 ----
    # main 原来只有合并退出码，反向基类的红绿没法独立跑。单检查只打印自己那条，退出码仍按
    # "0 通过 / 1 有发现 / 2 判据自身失效"报。
    if args.only:
        if args.only == "reverse_base":
            hits, problems = check_reverse_base(product_dir)
            print(f"扫描命中反向基类文件 {hits} 个")
            if hits < 1:
                print("反向基类检查：一个反向产物都没扫到（空扫不恒真，判红）", file=sys.stderr)
                problems = list(problems) + ["反向基类检查没有命中任何反向产物"]
            for problem in problems:
                print(problem, file=sys.stderr)
            print(f"{args.only} 问题 {len(problems)} 处")
            return 1 if problems else 0
        single = {
            "reverse_alignment": lambda: check_reverse_alignment(product_dir),
            "reverse_stubs": lambda: check_reverse_stubs(product_dir),
            "surface": lambda: check_surface(product_dir),
            "alignment": lambda: check_alignment(product_dir),
            "stubs": lambda: check_stubs(product_dir)[3] + check_stubs(product_dir)[5],
            "shared_value_types": lambda: check_shared_value_types(product_dir),
            "error_codes": lambda: check_error_codes(product_dir),
            "coverage": lambda: check_coverage(product_dir, read_expected()[0]),
        }.get(args.only)
        if single is None:
            print(f"未知或需要 full 模式的单检查名：{args.only}", file=sys.stderr)
            return 2
        problems = single()
        for problem in problems:
            print(problem, file=sys.stderr)
        print(f"{args.only} 问题 {len(problems)} 处")
        return 1 if problems else 0
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

    # ---- 结构桩判据（任务 1.3）：真实接口一个桩都不许有；夹具桩只打印，夹具丢返回值要判 ----
    real_count, fixture_count, fixture_stubs, stub_problems, dropped_problems, structure_problems, \
        fixture_dropped = check_stubs(product_dir)
    if real_count == 0:
        print("算不出真实接口 —— 判据自身失效（产物接口减去夹具头里声明的接口后是空集，"
              "「真实接口桩数为 0」会因此恒真）", file=sys.stderr)
        return 2
    real_names = sorted(p.name[: -len(".Bindings.cpp")] for p in product_dir.glob("*.Bindings.cpp")
                        if not is_registry_product(p)
                        and not is_reverse_product(p)
                        and p.name[: -len(".Bindings.cpp")] not in fixture_interface_names())
    print(f"被判真实接口：{', '.join(real_names)}")
    for problem in structure_problems:
        print(problem, file=sys.stderr)
    for problem in stub_problems:
        print(problem, file=sys.stderr)
    for problem in dropped_problems:
        print(problem, file=sys.stderr)
    for problem in fixture_dropped:
        print(problem, file=sys.stderr)
    print(f"真实接口桩数 {len(stub_problems)} / 丢返回值 {len(dropped_problems)} / "
          f"结构问题 {len(structure_problems)}")
    print(f"夹具接口 {fixture_count} 个、桩数 {fixture_stubs} 个、丢返回值 {len(fixture_dropped)} 个"
          f"（桩只打印，丢返回值参与判定）")

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

    # ---- 原生形态夹具（本 change 任务 2.2 起逐步加判据）----
    native_shape_problems = check_native_shapes(product_dir)
    for problem in native_shape_problems:
        print(problem, file=sys.stderr)
    print(f"原生形态夹具问题 {len(native_shape_problems)} 处")

    # ---- 反向产物判据（csharp-reverse-interface-binding 2.1 / 2.2）：替代判据，正向判据不动 ----
    # 命中数 0 也必须红：`check_bindings.py` 扫了一个没有反向产物的目录时，"反向基类干净"是恒真的。
    reverse_hits, reverse_base_problems = check_reverse_base(product_dir)
    if reverse_hits < 1:
        reverse_base_problems = list(reverse_base_problems) + [
            "反向基类检查没有命中任何反向产物（空扫不恒真，判红）"]
    for problem in reverse_base_problems:
        print(problem, file=sys.stderr)
    print(f"反向基类问题 {len(reverse_base_problems)} 处（扫描命中反向基类文件 {reverse_hits} 个）")
    reverse_alignment_problems = check_reverse_alignment(product_dir)
    for problem in reverse_alignment_problems:
        print(problem, file=sys.stderr)
    print(f"反向两侧对齐问题 {len(reverse_alignment_problems)} 处")
    reverse_stub_problems = check_reverse_stubs(product_dir)
    for problem in reverse_stub_problems:
        print(problem, file=sys.stderr)
    print(f"反向代理空体问题 {len(reverse_stub_problems)} 处")

    # 5.1：桥与它的表面判据一起退役（原生五类表面的判据在 check_surface_matrix / 原生夹具里）。

    # ---- 登记表（本 change 任务 1.3 / 6.2）：编号对账 + EngineApi 两侧 9 字段 + v10 / 56 字节 ----
    registry_problems = check_registry(product_dir, host_numbers,
                                       require_engine_api_ids=True, contiguous_from=1)
    for problem in registry_problems:
        print(problem, file=sys.stderr)
    print(f"登记表问题 {len(registry_problems)} 处")
    fixture_registry_problems: list[str] = []
    fixture_registry_dir = product_dir / "fixture"
    if fixture_registry_dir.is_dir():
        fixture_registry_problems = check_registry(fixture_registry_dir, fixture_numbers,
                                                   require_engine_api_ids=False, contiguous_from=8)
        for problem in fixture_registry_problems:
            print(problem, file=sys.stderr)
        print(f"夹具登记表问题 {len(fixture_registry_problems)} 处")

    failed = bool(mismatches or alignment_problems or stub_problems
                  or dropped_problems or structure_problems or coverage_problems or surface_problems
                  or matrix_problems or code_problems or shared_problems or foundation_problems
                  or qualified_problems or fixture_problems
                  or registry_problems or fixture_registry_problems
                  or fixture_dropped or native_shape_problems
                  or reverse_base_problems or reverse_alignment_problems or reverse_stub_problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
