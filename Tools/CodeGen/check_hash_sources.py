"""扫出引擎里"当哈希键用"的类型，逐个打印它的**算法来源**；有来源不明的就非零退出。

用法（在仓库根执行）：

    python Tools/CodeGen/check_hash_sources.py              # 只打印汇总与来源不明的那些
    python Tools/CodeGen/check_hash_sources.py --list-all    # 把每个键类型与它的来源都列出来

它扫这几种写法（正则见 SOURCE_PATTERNS）：`HashObj<X>`、`cacore::hash<X>`、`cacore::hash_256<X>`、
`unordered_map<X,`、`unordered_set<X,`、`shared_dic<X,`、`HashDictionary<X,`。

判据（`--list-all` 里每个类型后面那一列就是它）：

    生成物      类型名在 `build.py` 的接入清单 `CODEGEN_ANNOTATED` 里（构建期由 CodeGen 出字段级特化）
    手写声明    源码里看得见 `custom_hash_trait<该类型>` 或 `CA_HASH_BY_BYTES(该类型)`
    内置档      标量 / 枚举之类，或者该名字在某个模块的 `*Includes.h` 里出现过（第三方类型在那里声明）
    无          **来源不明** —— 这正是本脚本要抓的：某个编译单元把它当键用，却没有任何显式来源

退出码：`0` = 每个键类型都有来源；`1` = 有来源不明的（逐个点名）；`2` = 判据自身失效（找不到清单或源码）。
"""

import argparse
import re
import sys
from pathlib import Path

# 控制台默认代码页编不出中文（本机是 cp1252），会以 UnicodeEncodeError 结束 —— 与 `build.py` 同样的处置。
for _stream in (sys.stdout, sys.stderr):
    _stream.reconfigure(encoding="utf-8", errors="replace")

REPO = Path(__file__).resolve().parents[2]
MANIFEST_FILE = REPO / "build.py"

# 扫哪些后缀的源码；`openspec/` 与构建目录整棵排除（那里是历史与产物，不是要被改的代码）。
SOURCE_SUFFIXES = (".h", ".hpp", ".hh", ".cpp", ".cxx", ".cc")
EXCLUDE_DIRS = {"openspec", "out", ".git", ".claude", ".review_tmp", "_deps", "Tools"}

# 「谁被当键用」的写法。捕获组 1 是类型名（去掉了 `cacore::` / `graphics_backend::` 这类限定前缀，
# 与清单里的写法对齐）。
SOURCE_PATTERNS = (
    re.compile(r"\bHashObj<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bcacore::hash<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bcacore::hash_256<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bunordered_map<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bunordered_set<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bshared_dic<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bHashDictionary<\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
)

# 「显式来源」的写法：手写特化与按字节声明宏。限定名前允许一个前导 `::`（仓库里的写法就是
# `custom_hash_trait<::cacore::NameHash>`）。
TRAIT_PATTERNS = (
    re.compile(r"\bcustom_hash_trait<\s*(?:::)?\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
    re.compile(r"\bCA_HASH_BY_BYTES\(\s*(?:::)?\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)"),
)

# `using X = …` 的别名：值语义的包装（`HashObj<…>` / `TypedVKHashVal<…>`）自带来源。
ALIAS_PATTERN = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);")
ALIAS_SOURCE_HINT = re.compile(r"\b(HashObj|TypedVKHashVal|NameHash|PathHash)\b")

# 容器自己带哈希器时（`unordered_map<K, V, PipelineLibraryKeyHash, …>`）不走运行时的分派链。
OWN_HASHER_PATTERN = re.compile(r"^\s*,\s*[A-Za-z_]\w*(Hash|Equal|Hasher)\b")

# 模板参数名不是真实类型，看到就跳过（它们是「写法」，不是「谁被当键用」）。
TEMPLATE_PARAMS = {"T", "TKey", "TValue", "TObj", "ObjType", "Key", "Value", "DescType", "V", "E", "K", "H", "C"}

# 标量：运行时里本来就有那一档（按自身宽度算），不需要任何声明。
SCALARS = {
    "bool", "char", "signed", "unsigned", "short", "int", "long", "float", "double",
    "size_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "VkFlags", "VkBool32",
}

# 智能指针（当键时按地址算）与第三方句柄 typedef（是裸指针，按地址算）。
SMART_POINTERS = {"shared_ptr", "unique_ptr", "weak_ptr"}
THIRD_PARTY_HANDLES = {"VmaAllocation"}

# 例外清单：**本仓库定义、却没有任何显式来源**的键类型，一行一个（`类型名  # 理由`）。
# 它必须与脚本一起被看见：每次运行都会把里面的条目逐条打印出来。往里加东西等于宣布
# "这个类型今天仍然没有来源"，所以每条都要写清为什么、以及谁来收尾。
EXCEPTIONS_FILE = Path(__file__).resolve().parent / "hash_source_exceptions.txt"


def load_exceptions() -> dict[str, str]:
    entries: dict[str, str] = {}
    if not EXCEPTIONS_FILE.exists():
        return entries
    for line in EXCEPTIONS_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, reason = line.partition("#")
        entries[name.strip()] = reason.strip()
    return entries


def source_files() -> list[Path]:
    out = []
    for p in REPO.rglob("*"):
        if not p.is_file() or p.suffix not in SOURCE_SUFFIXES:
            continue
        rel = p.relative_to(REPO)
        if any(part in EXCLUDE_DIRS for part in rel.parts):
            continue
        out.append(p)
    return out


def manifest_type_names() -> set[str]:
    """`build.py` 接入清单里的类型名（短名，嵌套类型取最内层）。"""
    text = MANIFEST_FILE.read_text(encoding="utf-8", errors="replace")
    start = text.find("CODEGEN_ANNOTATED = [")
    if start < 0:
        return set()
    end = text.find("\n]", start)
    block = text[start:end if end > 0 else len(text)]
    names = set()
    for m in re.finditer(r'^\s*\("([^"]+)",\s*"([^"]+)"', block, re.MULTILINE):
        names.add(m.group(2).split("::")[-1])
    return names


def includes_header_text() -> str:
    """模块 `*Includes.h` 的正文：第三方类型在那里声明，算「内置档」。

    ⚠ 注释里的写法不算数 —— 本脚本的红态自检就是"把某一行声明注释掉，它必须变红"。
    """
    chunks = []
    for p in REPO.rglob("*Includes.h"):
        if any(part in EXCLUDE_DIRS for part in p.relative_to(REPO).parts):
            continue
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            code = line.split("//")[0]
            if code.strip():
                chunks.append(code)
    return "\n".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description="哈希键类型的算法来源盘点")
    parser.add_argument("--list-all", action="store_true", help="把每个键类型与它的来源都列出来")
    args = parser.parse_args()

    manifest = manifest_type_names()
    if not manifest:
        print(f"找不到接入清单（{MANIFEST_FILE}）—— 判据自身失效，不当成通过")
        return 2

    files = source_files()
    if not files:
        print("扫不到任何源码文件 —— 判据自身失效，不当成通过")
        return 2

    traits: set[str] = set()
    aliases: set[str] = set()
    keys: dict[str, set[str]] = {}   # 类型名 -> 出现它的文件（相对路径）
    for path in files:
        try:
            raw = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        rel = str(path.relative_to(REPO)).replace("\\", "/")
        for line in raw.splitlines():
            code = line.split("//")[0]          # 注释里的写法不算数（`ShaderSourceInfo` 就只在注释里）
            if not code.strip():
                continue
            for pat in TRAIT_PATTERNS:
                for m in pat.finditer(code):
                    traits.add(m.group(1))
            for m in ALIAS_PATTERN.finditer(code):
                if ALIAS_SOURCE_HINT.search(m.group(2)):
                    aliases.add(m.group(1))
            for pat in SOURCE_PATTERNS:
                for m in pat.finditer(code):
                    name = m.group(1)
                    if name in TEMPLATE_PARAMS:
                        continue
                    tail = code[m.end(1):]
                    if re.match(r"\s*(?:const\s*)?\*", tail):
                        continue                # 指针当键：运行时按地址算，不需要声明
                    if re.search(r"\b[A-Za-z_]\w*(Hash|Equal|Hasher)\b", tail):
                        continue                # 容器自己带了哈希器，不走运行时的分派链
                    keys.setdefault(name, set()).add(rel)

    includes = includes_header_text()
    exceptions = load_exceptions()

    rows = []
    unknown = []
    excepted = []
    for name in sorted(keys):
        if name in manifest:
            source = "生成物"
        elif name in traits:
            source = "手写声明"
        elif name in aliases:
            source = "内置档"
        elif name in SCALARS or re.match(r"^[A-Z][A-Z0-9_]+$", name):
            source = "内置档"
        elif name.endswith("Flags") or name.endswith("FlagBits"):
            # Vulkan-Hpp 的 flag 类型是 `vk::Flags<…>` 的别名，由 `VulkanIncludes.h` 里那条**泛化**特化覆盖。
            source = "内置档"
        elif name in SMART_POINTERS or name in THIRD_PARTY_HANDLES:
            source = "内置档"
        elif re.search(rf"\b{re.escape(name)}\b", includes):
            source = "内置档"
        elif name in exceptions:
            source = "例外"
            excepted.append(name)
        else:
            source = "无"
            unknown.append(name)
        rows.append((name, source, len(keys[name])))

    print(f"扫描 {len(files)} 个文件、命中 {len(unknown)} 个来源不明的键类型")
    if args.list_all:
        for name, source, count in rows:
            print(f"  {source:<8} {name:<44} {count} 处")
    elif unknown:
        for name in unknown:
            where = ", ".join(sorted(keys[name])[:3])
            print(f"  来源不明: {name}（出现在 {where}）")
    if excepted:
        print(f"例外 {len(excepted)} 个（{EXCEPTIONS_FILE.name}，逐条列出）：")
        for name in excepted:
            where = ", ".join(sorted(keys[name])[:3])
            print(f"  {name}：{exceptions[name]}（出现在 {where}）")
    print(f"清单 {len(manifest)} 个类型；键类型共 {len(rows)} 个")
    return 1 if unknown else 0


if __name__ == "__main__":
    sys.exit(main())
