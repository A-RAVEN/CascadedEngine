#!/usr/bin/env python3
"""C# 结构化缓冲像素与提交前拒绝证据的对账（csharp-render-structured-buffer 任务 1.2 / 4.1）。

判据分两半，缺一半都不算通过：

  ① 正常证据：两个后端各要有一份 `status = pass` 的 `TestCSharpStructuredBuffer` 报告，
     并且同一次运行的 PNG 存在且非空（像素判据必须是真实读回来的图，不是"跑过就算"）；
  ② 负向证据：`bad-buffer-color` / `unbound-field` / `bad-buffer-name` 三个模式 × 两个后端，
     必须 `status = fail`，且 `error` 里的错误码与模式一一对应。负向被"错误接受"
     （变成 pass）同样非零。

两种负向的判据都是**提交前拒绝**，不是像素：
    `SubmitGraph` 返回非零、后端执行次数为 0、错误文本点名未写字段或缓冲名。
    所以 `unbound-field` / `bad-buffer-name` 既要求错误码 `E_SB_BIND`，也要求错误里带
    "后端执行次数 0" 与对应名字，并且**不要求、也不读取** PNG —— 未定义内容的像素不能当判据。
    `bad-buffer-color` 才是像素判据：中心 MUST 精确等于 `(0,0,255,255)`（`E_SB_PIXEL`）。

报告约定（`GPUBackendTester.exe --report <path>` 原样写出）：
    test_output/csharp_structured_buffer_<backend>_<mode>.json
      backend ∈ {d3d12, vulkan}
      mode    ∈ {normal, bad-buffer-color, unbound-field, bad-buffer-name}
PNG 约定（测试自己写的）：
    test_output/TestCSharpStructuredBuffer_<backend>_normal.png

退出码：
  0 = 正常与负向证据全部对上
  1 = 缺证据 / 正常不是 pass / 负向被接受 / 错误码或读数不对（逐条打印）
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
# 模式 -> (期望错误码, error 里必须出现的原文)。None = 正常模式，要求 status = pass。
MODES = {
    "normal": None,
    # 缓冲数据改写为 (0,0,1)：中心精确等于 (0,0,255,255) 才证明缓冲数据进了输出。
    "bad-buffer-color": ("E_SB_PIXEL", "(0,0,255,255)"),
    # 字段从未 SetBuffer：SubmitGraph 在进入后端执行前拒绝并点名未写字段。
    "unbound-field": ("E_SB_BIND", "colorStructuredBuffer"),
    # 绑了未在本图登记的缓冲名：SubmitGraph 在进入后端执行前拒绝并点名缓冲名。
    "bad-buffer-name": ("E_SB_BIND", "sbColorsMissing"),
}
# 两种提交前拒绝都必须证明"后端执行次数为 0"。
BIND_REQUIRED_TEXT = "后端执行次数 0"
TEST_NAME = "TestCSharpStructuredBuffer"


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


REPO = find_repo_root(Path(__file__).resolve())


def report_path(report_dir: Path, backend: str, mode: str) -> Path:
    return report_dir / f"csharp_structured_buffer_{backend}_{mode}.json"


def png_path(report_dir: Path, backend: str) -> Path:
    return report_dir / f"TestCSharpStructuredBuffer_{backend}_normal.png"


def read_result(path: Path) -> dict | None:
    """返回 `TestCSharpStructuredBuffer` 那一条结果；文件存在但解析不了返回 None。"""
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
    parser = argparse.ArgumentParser(description="对账 C# 结构化缓冲的正常与负向证据")
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
        for mode, expected in MODES.items():
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
            if expected is None:
                if status != "pass":
                    problems.append(f"正常证据不是 pass：{backend} 的 {mode} 是 {status}，error={error}")
                png = png_path(report_dir, backend)
                if not png.is_file() or png.stat().st_size == 0:
                    problems.append(f"缺正常像素 PNG 或为空：{png}")
                lines.append(f"{backend:<7} {mode:<18} status={status}")
                continue
            expected_code, required_text = expected
            if status != "fail":
                problems.append(
                    f"负向结果被错误接受：{backend} 的 {mode} 是 {status}（期望 fail，错误码 {expected_code}）")
            if expected_code not in error:
                problems.append(
                    f"负向错误码不对：{backend} 的 {mode} 期望 {expected_code}，实际 error={error}")
            if required_text not in error:
                problems.append(
                    f"{mode} 的 error 里缺「{required_text}」：{backend} 的实际 error={error}")
            if mode != "bad-buffer-color" and BIND_REQUIRED_TEXT not in error:
                problems.append(
                    f"{mode} 没有证明提交前拒绝：{backend} 的 error 里缺「{BIND_REQUIRED_TEXT}」，"
                    f"实际 error={error}")
            lines.append(f"{backend:<7} {mode:<18} status={status} code={expected_code}")

    summary = "\n".join(lines)
    print("C# 结构化缓冲证据对账")
    print(summary)
    print(f"问题 {len(problems)} 处")
    for problem in problems:
        print("  ✗ " + problem)

    log_path = report_dir / "check_csharp_structured_buffer.log"
    try:
        log_path.write_text(
            "C# 结构化缓冲证据对账（报告目录 " + str(report_dir) + "）\n" + summary +
            "\n问题 " + str(len(problems)) + " 处\n" +
            "\n".join("  ✗ " + problem for problem in problems) + "\n",
            encoding="utf-8")
    except OSError as exc:
        print(f"写不了对账日志 {log_path}：{exc}", file=sys.stderr)
        return 2

    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
