#!/usr/bin/env python3
"""普通 `ExecuteGraph` 不得承担计时开销（vulkan-gpu-queries / 能力 gpu-graph-timing，任务 5.1）。

判据（design D6）：两个后端**普通 `ExecuteGraph` 的函数体**里不得出现计时入口或查询资源创建
调用。只有 `ExecuteGraphTimed` 才允许创建查询堆/池、记录时间戳、等待回读。

退出码：
  0 = 两个后端的普通 ExecuteGraph 函数体都干净（判据成立）
  1 = 至少一个函数体里出现了禁止的调用（判据被违反）
  2 = 判据自身失效（找不到被检查的文件或函数体）—— 此时退出码 1 没有意义

做法：读取源文件 → 剥注释 → 定位 `void <类>::ExecuteGraph(` 的签名 → 用花括号配平取出函数体
→ 在函数体里查禁用标记。禁用标记按"后端 + 通用"分两组，命中任意一个即判违反。
"""

import re
import sys
from pathlib import Path

# Windows 控制台默认不是 UTF-8，中文输出会抛 UnicodeEncodeError 把结果吞掉
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# (后端名, 源文件（相对仓库根）, 函数签名前缀)
TARGETS = [
    ("d3d12", "D3D12RenderBackend/private/RenderBackend_D3D12.cpp", "void RenderBackend_D3D12::ExecuteGraph("),
    ("vulkan", "VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp", "void RenderBackend_Vulkan::ExecuteGraph("),
]

# 禁用标记：命中任意一个就说明普通入口接上了计时路径
FORBIDDEN = [
    "ExecuteGraphTimed",     # 计时入口本身
    "GetTimingCapabilities", # 计时能力查询（普通路径不需要）
    "D3D12GPUTiming",        # D3D12 的查询资源 / 结果对象
    "VulkanGPUTiming",       # Vulkan 的查询资源 / 结果对象
    "CreateQueryHeap",       # D3D12 时间戳查询堆创建
    "createQueryPool",       # Vulkan 时间戳查询池创建
    "SetTimingRecorder",     # 把计时器挂到执行器上
]

COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


def strip_comments(text: str) -> str:
    """注释里的示例代码不算调用点：先剥注释再取函数体。"""
    return COMMENT_RE.sub("", text)


def extract_function_body(text: str, signature_prefix: str):
    """返回 signature_prefix 那个函数的函数体文本（不含最外层花括号）；找不到返回 None。

    签名里可能有换行（Vulkan 的 ExecuteGraph 是两行签名），所以按前缀定位后在接下来的文本里找
    第一个 `{`（中间只允许空白与参数列表字符），再用花括号配平。
    """
    start = text.find(signature_prefix)
    if start < 0:
        return None
    brace = text.find("{", start)
    if brace < 0:
        return None
    # 签名与 `{` 之间不能出现 `;`（那说明找到的是声明，不是定义）
    if ";" in text[start:brace]:
        return None
    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    return None


def main() -> int:
    repo = find_repo_root(Path(__file__).resolve())
    problems = []
    checked = 0

    for backend, relative_path, signature in TARGETS:
        path = repo / relative_path
        if not path.is_file():
            print(f"找不到 {backend} 的后端源文件：{relative_path} —— 判据自身失效", file=sys.stderr)
            return 2
        body = extract_function_body(strip_comments(path.read_text(encoding="utf-8", errors="replace")), signature)
        if body is None:
            print(f"{backend}: 在 {relative_path} 里找不到普通 ExecuteGraph 的函数体 —— 判据自身失效",
                  file=sys.stderr)
            return 2
        # 非空判据：普通 ExecuteGraph 的真实函数体一定会把图交给执行器（`CompileAndExecute`）。
        # 没有它说明取到的不是那段代码（例如签名匹配到了别处），此时"干净"没有意义。
        if "CompileAndExecute" not in body:
            print(f"{backend}: 取到的 ExecuteGraph 函数体里没有 CompileAndExecute，判据不可信 —— 判据自身失效",
                  file=sys.stderr)
            return 2
        checked += 1

        hits = [token for token in FORBIDDEN if token in body]
        if hits:
            problems.append(
                f"{backend} 的普通 ExecuteGraph 函数体里出现了计时入口 / 查询资源创建调用：" + "、".join(hits)
            )
        else:
            print(f"{backend}: 普通 ExecuteGraph 函数体干净（{relative_path}）")

    total = len(TARGETS)
    if problems:
        print(f"检查 {checked}/{total} 个后端、命中 {len(problems)} 处问题")
        for problem in problems:
            print("  ✗ " + problem)
        return 1

    print(f"检查 {checked}/{total} 个后端、0 处问题：普通 ExecuteGraph 不创建计时资源（design D6）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
