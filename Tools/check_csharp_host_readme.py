#!/usr/bin/env python3
"""对拍 `CSharpHost/README.md` 与手写 ABI 的真实实现（csharp-render-shader-constants 任务 4.2）。

为什么需要它：`Tools/CodeGen/verify_readme.py` 只重跑 `Tools/CodeGen/README.md` 里的命令，
`CSharpHost/README.md` 里的 ABI 版本、接口编号表与探针开关清单**没有任何命令判据** ——
文档写错一行照样全绿。这个脚本把文档里的三样读数对拍代码真实值：

  ① ABI 版本：README 的 `vN` == `Host.cs` 的 `ExpectedAbiVersion` == `CSharpHost.cpp` 的 `s_engineApiVersion`；
  ② 函数表大小：README 的字节数 == `CSharpHost.cpp` 里 `static_assert(sizeof(EngineApi) == N)` 的 N
     （那条 static_assert 由编译器核对，所以 N 是"实测值"而不是文档自己写的数）；
     ②b 全文读数：README **每一处**「函数表 N 字节」「ABI vN」「sizeof(EngineApi) == Nu」
     「托管侧期望 N」都必须等于上面两个值，出现「不是 N」而 N 就是真实值时也算不符 ——
     此前只读头部那一行，散文示例里的旧读数漏网（csharp-render-texture-sampling 任务 3.1）；
  ③ 接口编号表：README 每一行的编号与接口名 == `CSharpHost.cpp` 的 `s_interfaceId*` 常量，
     且行数 == `EngineApi.cs` 里下发的接口编号字段数；
  ④ 探针开关：`Host.cs` / `UserScript.cs` 里读的环境变量（`CASCADED_*`）每一个都在 README 的
     环境变量表里出现。

退出码：
  0 = 三样全部对上
  1 = 有对不上的条目（逐条打印"文档写什么 / 代码是什么"并点名条目）
  2 = 判据自身失效（找不到文件，或某份文件里解析不到预期结构）
"""
from __future__ import annotations

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
ENGINE_API_CS = REPO / "Scripting" / "Managed" / "EngineApi.cs"
USER_SCRIPT_CS = REPO / "Scripting" / "Managed" / "UserScript.cs"
HOST_CPP = REPO / "CSharpHost" / "private" / "CSharpHost.cpp"


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


def parse_host_expected_version(text: str) -> int:
    match = re.search(r"ExpectedAbiVersion\s*=\s*(\d+)\s*;", text)
    if not match:
        raise JudgeFailure("Host.cs 里解析不到 ExpectedAbiVersion")
    return int(match.group(1))


def parse_cpp_version(text: str) -> int:
    match = re.search(r"s_engineApiVersion\s*=\s*(\d+)\s*;", text)
    if not match:
        raise JudgeFailure("CSharpHost.cpp 里解析不到 s_engineApiVersion")
    return int(match.group(1))


def parse_cpp_size(text: str) -> int:
    match = re.search(r"static_assert\s*\(\s*sizeof\s*\(\s*EngineApi\s*\)\s*==\s*(\d+)", text)
    if not match:
        raise JudgeFailure("CSharpHost.cpp 里解析不到 static_assert(sizeof(EngineApi) == N)")
    return int(match.group(1))


def parse_cpp_interface_ids(text: str) -> dict[str, int]:
    """返回 {常量名: 编号}，例如 {'s_interfaceIdWindow': 1, …}。"""
    table = {}
    for name, value in re.findall(
            r"static\s+constexpr\s+uint32_t\s+(s_interfaceId\w+)\s*=\s*(\d+)\s*;", text):
        table[name] = int(value)
    if not table:
        raise JudgeFailure("CSharpHost.cpp 里解析不到 s_interfaceId* 常量")
    return table


def parse_engine_api_id_fields(text: str) -> list[str]:
    fields = re.findall(r"public\s+uint\s+(\w*InterfaceId)\s*;", text)
    if not fields:
        raise JudgeFailure("EngineApi.cs 里解析不到 *InterfaceId 字段")
    return fields


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
                    f"第 {index} 行：ABI v{value} ≠ s_engineApiVersion {version} —— {stripped}")
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
    try:
        readme_text = read(README)
        readme_version, readme_size = parse_readme_abi(readme_text)
        readme_interfaces = parse_readme_interfaces(readme_text)
        readme_env = parse_readme_env(readme_text)

        host_version = parse_host_expected_version(read(HOST_CS))
        cpp_text = read(HOST_CPP)
        cpp_version = parse_cpp_version(cpp_text)
        cpp_size = parse_cpp_size(cpp_text)
        cpp_ids = parse_cpp_interface_ids(cpp_text)
        api_fields = parse_engine_api_id_fields(read(ENGINE_API_CS))
        used_env = parse_env_names(read(HOST_CS)) | parse_env_names(read(USER_SCRIPT_CS))
    except JudgeFailure as exc:
        print(f"判据自身失效：{exc}", file=sys.stderr)
        return 2

    problems: list[str] = []

    # ① ABI 版本三方一致
    if not (readme_version == host_version == cpp_version):
        problems.append(
            f"ABI 版本不符：README 写 v{readme_version}、Host.cs ExpectedAbiVersion={host_version}、"
            f"CSharpHost.cpp s_engineApiVersion={cpp_version}")
    # ② 函数表大小（README vs 编译器核对的 static_assert）
    if readme_size != cpp_size:
        problems.append(
            f"函数表大小不符：README 写 {readme_size} 字节、CSharpHost.cpp 的 "
            f"static_assert(sizeof(EngineApi) == {cpp_size})")
    # ②b 全文读数（任务 3.1）：散文示例、失败样例里的旧值同样要红。
    problems.extend(scan_readme_readings(readme_text, cpp_version, cpp_size))

    # ③ 接口编号表 vs s_interfaceId* 常量 vs EngineApi.cs 字段数
    cpp_id_values = sorted(cpp_ids.values())
    readme_id_values = sorted(readme_interfaces)
    if readme_id_values != cpp_id_values:
        problems.append(
            f"接口编号表不符：README 有编号 {readme_id_values}、CSharpHost.cpp 的 s_interfaceId* 有 "
            f"{cpp_id_values}")
    if len(readme_interfaces) != len(api_fields):
        problems.append(
            f"接口编号数不符：README 表里 {len(readme_interfaces)} 行、EngineApi.cs 下发 "
            f"{len(api_fields)} 个 *InterfaceId 字段（{', '.join(api_fields)}）")
    # 每个编号的接口名必须与常量名对得上（编号 6 ↔ s_interfaceIdScriptShaderStruct ↔ EngineApi 字段）
    for number, interface_name in sorted(readme_interfaces.items()):
        constants = [name for name, value in cpp_ids.items() if value == number]
        if not constants:
            continue
        constant = constants[0]
        # 常量名去掉前缀的 `s_interfaceId` 后应与接口名末段有共同标识（大小写无关，取最长公共前缀除外）
        bare = constant[len("s_interfaceId"):].lower()
        compact = re.sub(r"[^a-z0-9]", "", interface_name.lower())
        if bare.lower() not in compact:
            problems.append(
                f"接口编号 {number} 的名字对不上：README 写 `{interface_name}`、"
                f"CSharpHost.cpp 常量 `{constant}`")

    # ④ 探针开关：代码里读的每个 CASCADED_* 都要在 README 里出现
    missing = sorted(name for name in used_env if name not in readme_env)
    if missing:
        problems.append("README 的环境变量表缺这些开关：" + "、".join(missing))

    print(f"ABI 版本：README v{readme_version} / Host.cs {host_version} / CSharpHost.cpp {cpp_version}")
    print(f"函数表大小：README {readme_size} 字节 / static_assert {cpp_size} 字节")
    print(f"接口编号表：README {len(readme_interfaces)} 行 {readme_id_values} / "
          f"CSharpHost.cpp {len(cpp_ids)} 个常量 / EngineApi.cs {len(api_fields)} 个字段")
    print(f"探针开关：代码里读到 {len(used_env)} 个，README 表里 {len(readme_env)} 个")
    print(f"问题 {len(problems)} 处")
    for problem in problems:
        print("  ✗ " + problem)
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
