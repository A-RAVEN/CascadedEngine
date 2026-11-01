#!/usr/bin/env python3
"""窗口系统绑定的两侧日志对拍（`complete-csharp-window-system-bindings` 任务 3.2 / 5.2）。

判据对象是**同一次测试进程写出的那一个日志文件**，不是源码里的方法名：

  1. `GetMonitor`：C++ 侧的 `[C++ GetMonitor] probe=N ...` 与托管侧的 `[托管侧 GetMonitor] probe=N ...`
     逐字段比对 —— 两个矩形的 x/y/width/height 与 DPI 一共 9 个数，一个不对就红；
  2. 无效显示器编号：`-1` 与 `GetMonitorCount()` 两个编号都必须在托管侧留下 `code=1006` 的读数，
     不允许"放过无效编号"（原生 clamp 会返回另一个显示器，日志里就看不出错误了）；
  3. `--callbacks`：10 类窗口/输入回调各有 C++ 触发行与托管回调行，窗口身份（指针值）与全部标量参数
     逐项一致；少一类就点名那一类。

退出码：0 = 全部对上；1 = 有缺失或数值不符（逐条打印）；2 = 判据自身失效（日志不存在 / 读不出夹具读数）。

红态怎么构造（都是运行期开关，不改源码）：
  · `CASCADED_CSHARP_SELFTEST=monitor-mismatch`   托管侧打印时把 m_MonitorRect.width 加 1 ⇒ 本脚本报字段不符；
  · `CASCADED_CSHARP_SELFTEST=monitor-skip-invalid` 跳过两次无效编号调用 ⇒ 本脚本报"缺少无效显示器读数"；
  · 临时屏蔽 `FakeWindowSystem` 里任意一类回调的保存/触发 ⇒ `--callbacks` 报缺那一类。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

CALLBACK_NAMES = [
    "SetWindowFocusCallback",
    "SetCursorEnterCallback",
    "SetCursorPosCallback",
    "SetMouseButtonCallback",
    "SetScrollCallback",
    "SetWindowCloseCallback",
    "SetWindowPosCallback",
    "SetWindowSizeCallback",
    "SetKeyCallback",
    "SetCharCallback",
]

CPP_MONITOR = re.compile(
    r"\[C\+\+ GetMonitor\] probe=(\d+) id=(-?\d+) rect=\(([^)]*)\) work=\(([^)]*)\) dpi=(\S+)")
CS_MONITOR = re.compile(
    r"\[托管侧 GetMonitor\] probe=(\d+) id=(-?\d+) rect=\(([^)]*)\) work=\(([^)]*)\) dpi=(\S+)")
CPP_COUNT = re.compile(r"\[C\+\+ GetMonitor\] count=(\d+)")
CS_INVALID = re.compile(r"\[托管侧 无效显示器\] id=(-?\d+) code=(\d+)")
CPP_CALLBACK = re.compile(r"\[C\+\+ 回调\] (\w+) window=(0x[0-9A-Fa-f]+) args=\(([^)]*)\)")
CS_CALLBACK = re.compile(r"\[托管侧回调\] (\w+) window=(0x[0-9A-Fa-f]+) args=\(([^)]*)\)")


def parse_window(text: str):
    try:
        return int(text, 16)
    except ValueError:
        return None


def split_numbers(text: str):
    return [part.strip() for part in text.split(",")] if text != "" else []


def main() -> int:
    parser = argparse.ArgumentParser(description="窗口系统绑定的 C++ / 托管两侧日志对拍")
    parser.add_argument("--log", required=True, help="GPUBackendTester 写出的测试日志（test_output/<名字>.log）")
    parser.add_argument("--callbacks", action="store_true", help="额外要求 10 类回调的两侧触发行")
    args = parser.parse_args()

    log_path = Path(args.log)
    if not log_path.is_file():
        print(f"找不到日志 {log_path} —— 判据自身失效", file=sys.stderr)
        return 2
    text = log_path.read_text(encoding="utf-8", errors="replace")

    problems: list[str] = []

    # ---- 1. GetMonitor 两侧逐字段对拍 ----
    cpp_monitors = {(match.group(1), match.group(2)): match for match in CPP_MONITOR.finditer(text)}
    cs_monitors = {(match.group(1), match.group(2)): match for match in CS_MONITOR.finditer(text)}
    if not cpp_monitors:
        problems.append("日志里没有 C++ 侧 GetMonitor 读数（[C++ GetMonitor] probe=...）")
    if not cs_monitors:
        problems.append("日志里没有托管侧 GetMonitor 读数（[托管侧 GetMonitor] probe=...）")
    compared = 0
    for key in sorted(cpp_monitors):
        if key not in cs_monitors:
            problems.append(f"probe={key[0]} id={key[1]}：托管侧没有对应的 GetMonitor 读数")
            continue
        cpp = cpp_monitors[key]
        cs = cs_monitors[key]
        fields = [("m_MonitorRect", cpp.group(3), cs.group(3)),
                  ("m_WorkAreaRect", cpp.group(4), cs.group(4)),
                  ("m_DPIScale", cpp.group(5), cs.group(5))]
        for name, cpp_value, cs_value in fields:
            if name == "m_DPIScale":
                if cpp_value != cs_value:
                    problems.append(f"probe={key[0]} {name} 不符：C++ {cpp_value} / 托管 {cs_value}")
                continue
            cpp_parts = split_numbers(cpp_value)
            cs_parts = split_numbers(cs_value)
            if len(cpp_parts) != 4 or len(cs_parts) != 4:
                problems.append(f"probe={key[0]} {name} 读数字段数不是 4：C++ {cpp_value} / 托管 {cs_value}")
                continue
            for axis, cpp_number, cs_number in zip(("x", "y", "width", "height"), cpp_parts, cs_parts):
                if cpp_number != cs_number:
                    problems.append(
                        f"probe={key[0]} {name}.{axis} 不符：C++ {cpp_number} / 托管 {cs_number}")
        compared += 1

    # ---- 2. 无效显示器编号必须走 1006 ----
    count_matches = CPP_COUNT.findall(text)
    if not count_matches:
        problems.append("日志里没有 [C++ GetMonitor] count=...（判据需要它来构造越界编号）")
    invalid_codes = {int(index): int(code) for index, code in CS_INVALID.findall(text)}
    required_invalid = [-1]
    if count_matches:
        required_invalid.append(int(count_matches[0]))
    for invalid in required_invalid:
        if invalid not in invalid_codes:
            problems.append(f"缺少无效显示器编号 {invalid} 的托管侧读数（放过无效编号 = 允许原生 clamp）")
        elif invalid_codes[invalid] != 1006:
            problems.append(f"无效显示器编号 {invalid} 的错误码是 {invalid_codes[invalid]}，期望 1006")

    # ---- 3. 10 类回调（可选）----
    callback_compared = 0
    if args.callbacks:
        cpp_callbacks = {}
        for match in CPP_CALLBACK.finditer(text):
            cpp_callbacks.setdefault(match.group(1), match)
        cs_callbacks = {}
        for match in CS_CALLBACK.finditer(text):
            cs_callbacks.setdefault(match.group(1), match)
        for name in CALLBACK_NAMES:
            cpp = cpp_callbacks.get(name)
            cs = cs_callbacks.get(name)
            if cpp is None:
                problems.append(f"回调 {name}：日志里没有 C++ 侧触发行（[C++ 回调] {name} ...）")
                continue
            if cs is None:
                problems.append(f"回调 {name}：日志里没有托管侧回调行（[托管侧回调] {name} ...）")
                continue
            cpp_window = parse_window(cpp.group(2))
            cs_window = parse_window(cs.group(2))
            if cpp_window is None or cs_window is None:
                problems.append(f"回调 {name}：窗口身份不是十六进制指针（C++ {cpp.group(2)} / 托管 {cs.group(2)}）")
            elif cpp_window != cs_window:
                problems.append(f"回调 {name}：窗口身份不符（C++ {cpp.group(2)} / 托管 {cs.group(2)}）")
            if split_numbers(cpp.group(3)) != split_numbers(cs.group(3)):
                problems.append(
                    f"回调 {name}：标量参数不符（C++ {cpp.group(3)} / 托管 {cs.group(3)}）")
            callback_compared += 1

    hit = len(problems)
    print(f"日志 {log_path}")
    print(f"GetMonitor 对拍 {compared} 组 / 回调对拍 {callback_compared} 类 / 命中 {hit} 处问题")
    for problem in problems:
        print("  ✗ " + problem)
    return 0 if hit == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
