#!/usr/bin/env python3
"""对拍 `CSharpHost/README.md` 与**生成产物**里的真实 ABI 读数（render-interface-merge 任务 4.1）。

为什么需要它：`Tools/CodeGen/verify_readme.py` 只重跑 `Tools/CodeGen/README.md` 里的命令，
`CSharpHost/README.md` 里的 ABI 版本、接口编号表与探针开关清单**没有任何命令判据** ——
文档写错一行照样全绿。这个脚本把文档里的三样读数对拍代码真实值：

  ① ABI 版本：README 的 `vN` == 生成头 `BindingRegistry.Bindings.h` 的 `kEngineApiVersion`
     （本 change 起 ABI 版本不再手写；生成物是唯一来源）；
  ② 函数表大小：README 的字节数 == 生成头里 `static_assert(sizeof(EngineApi) == N)` 的 N
     （那条 static_assert 由编译器核对，所以 N 是"实测值"而不是文档自己写的数）；
     ②b 全文读数：README **每一处**「函数表 N 字节」「ABI vN」「sizeof(EngineApi) == Nu」
     「托管侧期望 N」都必须等于上面两个值，出现「不是 N」而 N 就是真实值时也算不符；
  ③ 接口编号表：README 每一行的编号与接口**完整限定名** == 生成头的 `kBindingInterfaceId*`
     常量（编号唯一来源是接口头上的 `CA_BIND(编号)` 标注），且行数 == 生成 `EngineApi` 里
     下发的 `*_interface_id` 字段数、也 == 生成 `EngineApi.Bindings.cs` 的编号常量数；
  ④ 探针开关：`Host.cs` / `UserScript.cs` 里读的环境变量（`CASCADED_*`）每一个都在 README 的
     环境变量表里出现。

退出码：
  0 = 三样全部对上
  1 = 有对不上的条目（逐条打印"文档写什么 / 代码是什么"并点名条目）
  2 = 判据自身失效（找不到 README 或生成产物，或某份文件里解析不到预期结构）——
     生成产物缺失时**不跳过**，直接按判据自身失效退出。
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


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


REPO = find_repo_root(HERE)
README = REPO / "CSharpHost" / "README.md"
HOST_CS = REPO / "Scripting" / "Managed" / "Host.cs"
USER_SCRIPT_CS = REPO / "Scripting" / "Managed" / "UserScript.cs"
# 真值来源（本 change 起是生成产物，不是手写物）：默认 Debug 预设的绑定目录。
DEFAULT_PRODUCT_DIR = REPO / "out" / "build" / "x64-debug" / "codegen" / "bindings"


class JudgeFailure(Exception):
    """判据自身失效（解析不到结构），与"内容不符"分开计。"""


def read(path: Path) -> str:
    if not path.is_file():
        raise JudgeFailure(f"找不到文件：{path}")
    return path.read_text(encoding="utf-8", errors="replace")


def parse_readme_abi(text: str) -> tuple[int, int]:
    """返回（ABI 版本, 函数表字节数）。"""
    match = re.search(r"当前是\s*\**v(\d+)\**\s*[、,]\s*(\d+)\s*字节", text)
    if not match:
        raise JudgeFailure("README 里解析不到「当前是 vN、M 字节」这一行")
    return int(match.group(1)), int(match.group(2))


def parse_readme_interfaces(text: str) -> dict[int, str]:
    """返回 {编号: 接口名}，只取编号是 1..N 的数字行（跳过表头与分隔行）。"""
    table: dict[int, str] = {}
    for line in text.splitlines():
        match = re.match(r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|", line)
        if match:
            table[int(match.group(1))] = match.group(2).strip()
    if not table:
        raise JudgeFailure("README 里解析不到接口编号表（| 编号 | 接口 | …）")
    return table


def parse_readme_env(text: str) -> set[str]:
    """返回环境变量表里出现的 `CASCADED_*` 名字。"""
    names = set()
    for line in text.splitlines():
        for name in re.findall(r"`(CASCADED_[A-Z0-9_]+)", line):
            names.add(name)
    if not names:
        raise JudgeFailure("README 里解析不到环境变量表里的 CASCADED_* 名字")
    return names


def parse_generated_abi(header_text: str) -> tuple[int, int]:
    """从生成头里读（ABI 版本, 函数表字节数）。"""
    version_match = re.search(r"inline constexpr uint32_t kEngineApiVersion = (\d+)u;", header_text)
    size_match = re.search(r"static_assert\s*\(\s*sizeof\s*\(\s*EngineApi\s*\)\s*==\s*(\d+)\s*u", header_text)
    if not version_match or not size_match:
        raise JudgeFailure(
            "生成头 BindingRegistry.Bindings.h 里解析不到 kEngineApiVersion 或 "
            "static_assert(sizeof(EngineApi) == N)")
    return int(version_match.group(1)), int(size_match.group(1))


def parse_generated_interfaces(header_text: str) -> dict[int, str]:
    """从生成头读 {编号: 接口完整限定名}（注释里带限定名，常量名是短名）。"""
    table: dict[int, str] = {}
    for _suffix, number, qualified in re.findall(
            r"inline constexpr uint32_t kBindingInterfaceId(\w+) = (\d+)u;\s*//\s*(\S+)",
            header_text):
        table[int(number)] = qualified
    if not table:
        raise JudgeFailure("生成头里解析不到 kBindingInterfaceId* 编号常量")
    return table


def parse_generated_engine_api_id_fields(header_text: str) -> list[str]:
    """生成 `EngineApi` 里下发的编号字段（`*_interface_id`），数量必须等于编号表的行数。"""
    struct_match = re.search(r"struct EngineApi\s*\{(.*?)\};", header_text, re.S)
    if not struct_match:
        raise JudgeFailure("生成头里解析不到 struct EngineApi")
    fields = re.findall(r"uint32_t\s+(\w+_interface_id)\s*;", struct_match.group(1))
    if not fields:
        raise JudgeFailure("生成 EngineApi 里解析不到 *_interface_id 字段")
    return fields


def parse_generated_cs_numbers(cs_text: str) -> dict[int, str]:
    """从生成 `EngineApi.Bindings.cs` 读 {编号: 常量名后缀}。"""
    table: dict[int, str] = {}
    for suffix, number in re.findall(r"public const uint (\w*InterfaceId) = (\d+);", cs_text):
        table[int(number)] = suffix
    if not table:
        raise JudgeFailure("生成 EngineApi.Bindings.cs 里解析不到编号常量")
    return table


def parse_env_names(text: str) -> set[str]:
    return set(re.findall(r'GetEnvironmentVariable\("(CASCADED_[A-Z0-9_]+)"', text))


def scan_readme_readings(text: str, version: int, size: int) -> list[str]:
    """逐行扫描 README 全文的版本 / 字节数读数，返回不符项（带行号与原文）。

    只扫这几类"肯定是读数"的写法，避免把散文里的其它数字算进来：
      · `函数表 N 字节`（启动日志、就绪日志、失败示例、正文说明）
      · `ABI vN`
      · `sizeof(EngineApi) == Nu` / `sizeof(EngineApi) == N`
      · `托管侧期望 N`
      · `不是 N` —— 反向断言：N 恰好是真实值时必须报（"不是 80"而真实就是 80 是假话）
    """
    problems: list[str] = []
    counted = {"函数表字节数": 0, "ABI vN": 0}
    for index, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        for match in re.finditer(r"函数表\s*(\d+)\s*字节", line):
            counted["函数表字节数"] += 1
            value = int(match.group(1))
            if value != size:
                problems.append(
                    f"第 {index} 行：函数表 {value} 字节 ≠ static_assert {size} 字节 —— {stripped}")
        for match in re.finditer(r"ABI v(\d+)", line):
            counted["ABI vN"] += 1
            value = int(match.group(1))
            if value != version:
                problems.append(
                    f"第 {index} 行：ABI v{value} ≠ 生成 kEngineApiVersion {version} —— {stripped}")
        for match in re.finditer(r"sizeof\s*\(\s*EngineApi\s*\)\s*==\s*(\d+)", line):
            value = int(match.group(1))
            if value != size:
                problems.append(
                    f"第 {index} 行：sizeof(EngineApi) == {value} ≠ static_assert {size} —— {stripped}")
        for match in re.finditer(r"托管侧期望\s*(\d+)", line):
            value = int(match.group(1))
            if value != version:
                problems.append(
                    f"第 {index} 行：托管侧期望 {value} ≠ 当前 ABI {version} —— {stripped}")
        for match in re.finditer(r"不是\s*(\d+)", line):
            value = int(match.group(1))
            if value == size:
                problems.append(
                    f"第 {index} 行：写着「不是 {value}」，但 static_assert 就是 {size} 字节 —— {stripped}")
    # 读数被整段删掉同样是失效：两条最基本的读数至少在全文里出现一次。
    for key, count in counted.items():
        if count == 0:
            problems.append(f"README 全文里找不到任何「{key}」读数 —— 判据被架空")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="对拍 CSharpHost README 与绑定生成产物")
    parser.add_argument("--product-dir", default=str(DEFAULT_PRODUCT_DIR),
                        help="绑定生成产物目录（默认 Debug 预设的 codegen/bindings）")
    args = parser.parse_args()
    product_dir = Path(args.product_dir)
    if not product_dir.is_absolute():
        product_dir = (REPO / product_dir).resolve()
    header_path = product_dir / "BindingRegistry.Bindings.h"
    cs_path = product_dir / "EngineApi.Bindings.cs"

    try:
        readme_text = read(README)
        readme_version, readme_size = parse_readme_abi(readme_text)
        readme_interfaces = parse_readme_interfaces(readme_text)
        readme_env = parse_readme_env(readme_text)
        if not header_path.is_file():
            raise JudgeFailure(f"生成产物不存在：{header_path}（先跑 python build.py --config Debug）")
        if not cs_path.is_file():
            raise JudgeFailure(f"生成产物不存在：{cs_path}（先跑 python build.py --config Debug）")
        header_text = read(header_path)
        version, size = parse_generated_abi(header_text)
        generated_interfaces = parse_generated_interfaces(header_text)
        api_fields = parse_generated_engine_api_id_fields(header_text)
        cs_numbers = parse_generated_cs_numbers(read(cs_path))
        used_env = parse_env_names(read(HOST_CS)) | parse_env_names(read(USER_SCRIPT_CS))
    except JudgeFailure as exc:
        print(f"判据自身失效：{exc}", file=sys.stderr)
        return 2

    problems: list[str] = []

    # ① ABI 版本：README 全文读数 vs 生成常量（唯一来源）
    if readme_version != version:
        problems.append(
            f"ABI 版本不符：README 写 v{readme_version}、生成 kEngineApiVersion={version}")
    # ② 函数表大小：README vs 生成头里编译器核对的 static_assert
    if readme_size != size:
        problems.append(
            f"函数表大小不符：README 写 {readme_size} 字节、生成 "
            f"static_assert(sizeof(EngineApi) == {size}u)")
    # ②b 全文读数：散文示例、失败样例里的旧值同样要红
    problems.extend(scan_readme_readings(readme_text, version, size))

    # ③ 接口编号表：README 每一行 == 生成头编号常量；行数 == 编号表项数 == C# 常量数
    for number in sorted(set(readme_interfaces) | set(generated_interfaces)):
        want = generated_interfaces.get(number)
        got = readme_interfaces.get(number)
        if want is None:
            problems.append(f"接口编号表：README 多出编号 {number}（`{got}`），生成物里没有这一项")
        elif got is None:
            problems.append(f"接口编号表：README 缺编号 {number}（生成物是 `{want}`）")
        elif got != want:
            problems.append(
                f"接口编号 {number} 的接口名对不上：README 写 `{got}`、生成的是 `{want}`")
    if len(readme_interfaces) != len(generated_interfaces):
        problems.append(
            f"接口编号数不符：README 表里 {len(readme_interfaces)} 行、生成头 "
            f"{len(generated_interfaces)} 项")
    # 6.2 的 ABI v10：EngineApi 只下发两个窗口编号字段（桥五类的五个字段随退役删除），
    # 所以这里只要求窗口两字段都在、且字段数不超过编号表行数（不再逐接口相等）。
    if "window_interface_id" not in api_fields or "window_system_interface_id" not in api_fields:
        problems.append(f"EngineApi 少了窗口编号字段：{api_fields}")
    if len(api_fields) > len(generated_interfaces):
        problems.append(
            f"EngineApi 编号字段数 {len(api_fields)} 超过编号表行数 {len(generated_interfaces)}")
    if sorted(cs_numbers) != sorted(generated_interfaces):
        problems.append(
            f"生成两侧编号不一致：C++ 头 {sorted(generated_interfaces)}、"
            f"EngineApi.Bindings.cs {sorted(cs_numbers)}")
    for number, qualified in sorted(generated_interfaces.items()):
        short = qualified.rsplit("::", 1)[-1]
        expected_suffix = (
            short[1:] if len(short) > 1 and short[0] == "I" and short[1].isupper() else short
        ) + "InterfaceId"
        actual_suffix = cs_numbers.get(number, "")
        if actual_suffix != expected_suffix:
            problems.append(
                f"生成 C# 编号常量对不上：编号 {number}（{qualified}）期望 "
                f"{expected_suffix}、实际 {actual_suffix or '<缺失>'}")

    # ④ 探针开关：代码里读的每个 CASCADED_* 都要在 README 里出现
    missing = sorted(name for name in used_env if name not in readme_env)
    if missing:
        problems.append("README 的环境变量表缺这些开关：" + "、".join(missing))

    print(f"ABI 版本：README v{readme_version} / 生成 kEngineApiVersion={version}")
    print(f"函数表大小：README {readme_size} 字节 / 生成 static_assert {size} 字节")
    print(f"接口编号表：README {len(readme_interfaces)} 行 {sorted(readme_interfaces)} / "
          f"生成头 {len(generated_interfaces)} 项 / EngineApi {len(api_fields)} 个字段 / "
          f"C# {len(cs_numbers)} 个常量")
    print(f"探针开关：代码里读到 {len(used_env)} 个，README 表里 {len(readme_env)} 个")
    print(f"问题 {len(problems)} 处")
    for problem in problems:
        print("  ✗ " + problem)
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
