#!/usr/bin/env python3
"""C# 常量色（着色器参数）像素证据的对账（csharp-render-shader-constants 任务 1.2 / 4.1）。

判据分两半，缺一半都不算通过：

  ① 正常证据：两个后端各要有一份 `status = pass` 的 `TestCSharpConstantColor` 报告，
     并且同一次运行的 PNG 存在且非空（像素判据必须是真实读回来的图，不是"跑过就算"）；
  ② 负向证据：`bad-struct-colors` 与 `skip-param` 两个后端各两份，必须 `status = fail`，
     且 `error` 里的错误码与模式一一对应。负向被"错误接受"（变成 pass）同样非零。

`skip-param` 的判据是**提交前拒绝**，不是像素：
    `SubmitGraph` 返回非零、后端执行次数为 0、错误文本点名 `constantColorBlock`。
    所以这一模式既要求错误码 `E_CONSTCOLOR_PARAM`，也要求错误里带"后端执行次数 0"，
    并且**不要求、也不读取** PNG —— 未定义内容的像素不能当判据。

报告约定（`GPUBackendTester.exe --report <path>` 原样写出）：
    test_output/csharp_constant_color_<backend>_<mode>.json
      backend ∈ {d3d12, vulkan}
      mode    ∈ {normal, bad-struct-colors, skip-param}
PNG 约定（测试自己写的）：
    test_output/TestCSharpConstantColor_<backend>_normal.png

退出码：
  0 = 正常与负向证据全部对上
  1 = 缺证据 / 正常不是 pass / 负向被接受 / 错误码不对（逐条打印）
  2 = 判据自身失效（--report-dir 不存在，或脚本自己算不出该查哪些文件）
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

BACKENDS = ("d3d12", "vulkan")
# 模式 -> 期望的错误码（None = 正常模式，要求 status = pass）
MODES = {
    "normal": None,
    # 三槽红/绿/蓝：中心不再是纯红，由原生像素判据拒绝。
    "bad-struct-colors": "E_CONSTCOLOR_PIXEL",
    # 完全不绑参数：SubmitGraph 在进入后端执行前拒绝并点名 constantColorBlock。
    "skip-param": "E_CONSTCOLOR_PARAM",
}
# `skip-param` 必须证明"提交前拒绝"：后端执行次数为 0（设计第二节，两后端空参数表执行会断点）。
SKIP_PARAM_REQUIRED_TEXT = "后端执行次数 0"
TEST_NAME = "TestCSharpConstantColor"


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


REPO = find_repo_root(Path(__file__).resolve())


def report_path(report_dir: Path, backend: str, mode: str) -> Path:
    return report_dir / f"csharp_constant_color_{backend}_{mode}.json"


def png_path(report_dir: Path, backend: str) -> Path:
    return report_dir / f"TestCSharpConstantColor_{backend}_normal.png"


def read_result(path: Path) -> dict | None:
    """返回 `TestCSharpConstantColor` 那一条结果；文件存在但解析不了返回 None。"""
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))
    except (OSError, json.JSONDecodeError):
        return None
    if isinstance(data, dict):
        data = [data]
    if not isinstance(data, list):
        return None
    for entry in data:
        if isinstance(entry, dict) and entry.get("name") == TEST_NAME:
            return entry
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="对账 C# 常量色的正常与负向证据")
    parser.add_argument("--report-dir", default=str(REPO / "test_output"),
                        help="GPUBackendTester 写出报告的目录（默认 test_output/）")
    parser.add_argument("--list", action="store_true", help="只打印需要哪些文件")
    args = parser.parse_args()

    report_dir = Path(args.report_dir)
    if not report_dir.is_absolute():
        report_dir = (Path.cwd() / report_dir).resolve()

    if args.list:
        for backend in BACKENDS:
            for mode in MODES:
                print(f"{report_path(report_dir, backend, mode)}")
        return 0

    if not report_dir.is_dir():
        print(f"报告目录不存在：{report_dir} —— 判据自身失效（先跑 GPUBackendTester）",
              file=sys.stderr)
        return 2

    problems: list[str] = []
    lines: list[str] = []
    for backend in BACKENDS:
        for mode, expected_code in MODES.items():
            report = report_path(report_dir, backend, mode)
            if not report.is_file():
                problems.append(f"缺报告：{report.relative_to(REPO) if report.is_relative_to(REPO) else report}"
                                f"（后端 {backend}、模式 {mode}）")
                lines.append(f"{backend:<7} {mode:<18} 缺报告")
                continue
            entry = read_result(report)
            if entry is None:
                problems.append(f"报告解析不出 {TEST_NAME}：{report}")
                lines.append(f"{backend:<7} {mode:<18} 解析失败")
                continue
            status = entry.get("status")
            error = str(entry.get("error", ""))
            if expected_code is None:
                if status != "pass":
                    problems.append(f"正常证据不是 pass：{backend} 的 {mode} 是 {status}，error={error}")
                png = png_path(report_dir, backend)
                if not png.is_file() or png.stat().st_size == 0:
                    problems.append(f"缺正常像素 PNG 或为空：{png}")
                lines.append(f"{backend:<7} {mode:<18} status={status}")
                continue
            if status != "fail":
                problems.append(
                    f"负向结果被错误接受：{backend} 的 {mode} 是 {status}（期望 fail，错误码 {expected_code}）")
            if expected_code not in error:
                problems.append(
                    f"负向错误码不对：{backend} 的 {mode} 期望 {expected_code}，实际 error={error}")
            if mode == "skip-param" and SKIP_PARAM_REQUIRED_TEXT not in error:
                problems.append(
                    f"skip-param 没有证明提交前拒绝：{backend} 的 error 里缺「{SKIP_PARAM_REQUIRED_TEXT}」，"
                    f"实际 error={error}")
            lines.append(f"{backend:<7} {mode:<18} status={status} code={expected_code}")

    summary = "\n".join(lines)
    print("C# 常量色证据对账")
    print(summary)
    print(f"问题 {len(problems)} 处")
    for problem in problems:
        print("  ✗ " + problem)

    log_path = report_dir / "check_csharp_constant_color.log"
    try:
        log_path.write_text(
            "C# 常量色证据对账（报告目录 " + str(report_dir) + "）\n" + summary +
            "\n问题 " + str(len(problems)) + " 处\n" +
            "\n".join("  ✗ " + problem for problem in problems) + "\n",
            encoding="utf-8")
    except OSError as exc:
        print(f"写不了对账日志 {log_path}：{exc}", file=sys.stderr)
        return 2

    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
