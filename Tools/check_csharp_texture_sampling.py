#!/usr/bin/env python3
"""C# 纹理采样像素与提交前拒绝证据的对账（csharp-render-texture-sampling 任务 1.2 / 4.1）。

判据分两半，缺一半都不算通过：

  ① 像素证据：两个后端 × 两个像素模式（`normal` 与 `bad-texture-color`）各要有一份对得上的
     报告与 PNG。`normal` 必须 `status = pass`；`bad-texture-color` 必须 `status = fail` 且错误码
     `E_TEX_PIXEL`、错误文本里带中心期望色 `(0,255,255,255)` —— 纹理字节改了、中心跟着精确变，
     才证明"字节 → 纹理 → 采样 → 像素"整条链参与。
  ② 提交前拒绝证据：`unbound-image` / `unbound-sampler` 两个模式 × 两个后端必须
     `status = fail`、错误码 `E_TEX_BIND`、点名 `testTexture` / `testSampler`，并且同时证明
     **本次提交执行增量 0**（`GetExecuteGraphCountForTest`）与**上传执行 1**（单列计数）。
     这两种形态**不读像素** —— 未定义内容的像素不能当判据，所以也不要求 PNG。

报告约定（`GPUBackendTester.exe --report <path>` 原样写出）：
    test_output/csharp_texture_sampling_<backend>_<mode>.json
      backend ∈ {d3d12, vulkan}
      mode    ∈ {normal, bad-texture-color, unbound-image, unbound-sampler}
PNG 约定（测试自己写，只对做像素判据的模式要求）：
    test_output/TestCSharpTextureSampling_<backend>_<mode>.png

退出码：
  0 = 八份证据全部对上
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
# 模式 -> 判据。`pixel` 标记做像素判据的模式（要求 PNG）。
MODES = {
    "normal": {"kind": "pixel", "code": None, "text": None},
    # 纹理四纹素全改 (0,255,255,255)：中心必须精确跟着变，否则整链没参与。
    "bad-texture-color": {"kind": "pixel", "code": "E_TEX_PIXEL", "text": "(0,255,255,255)"},
    # 跳过 SetImage：SubmitGraph 在进入后端执行前拒绝并点名 testTexture。
    "unbound-image": {"kind": "bind", "code": "E_TEX_BIND", "text": "testTexture"},
    # 跳过 SetSampler：SubmitGraph 在进入后端执行前拒绝并点名 testSampler。
    "unbound-sampler": {"kind": "bind", "code": "E_TEX_BIND", "text": "testSampler"},
}
# 两种提交前拒绝都必须证明「本次提交执行增量 0」，并说明上传执行是另计的 1 次。
BIND_REQUIRED_TEXTS = ("本次提交执行增量 0", "上传执行 1")
TEST_NAME = "TestCSharpTextureSampling"


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


REPO = find_repo_root(Path(__file__).resolve())


def report_path(report_dir: Path, backend: str, mode: str) -> Path:
    return report_dir / f"csharp_texture_sampling_{backend}_{mode}.json"


def png_path(report_dir: Path, backend: str, mode: str) -> Path:
    return report_dir / f"TestCSharpTextureSampling_{backend}_{mode}.png"


def read_result(path: Path) -> dict | None:
    """返回 `TestCSharpTextureSampling` 那一条结果；文件存在但解析不了返回 None。"""
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
    parser = argparse.ArgumentParser(description="对账 C# 纹理采样的正常与负向证据")
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
        for mode, spec in MODES.items():
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
            if spec["kind"] == "pixel":
                if mode == "normal":
                    # 正常模式的两个像素期望由测试自己精确断言；报告这一层只承载 pass/fail。
                    if status != "pass":
                        problems.append(f"正常证据不是 pass：{backend} 的 {mode} 是 {status}，error={error}")
                else:
                    if status != "fail":
                        problems.append(
                            f"负向结果被错误接受：{backend} 的 {mode} 是 {status}"
                            f"（期望 fail，错误码 {spec['code']}）")
                    if spec["code"] not in error:
                        problems.append(
                            f"负向错误码不对：{backend} 的 {mode} 期望 {spec['code']}，实际 error={error}")
                    if spec["text"] not in error:
                        problems.append(
                            f"{mode} 的 error 里缺「{spec['text']}」：{backend} 的实际 error={error}")
                png = png_path(report_dir, backend, mode)
                if not png.is_file() or png.stat().st_size == 0:
                    problems.append(f"缺像素 PNG 或为空：{png}")
                lines.append(f"{backend:<7} {mode:<18} status={status}")
                continue
            # 提交前拒绝两种形态。
            if status != "fail":
                problems.append(
                    f"负向结果被错误接受：{backend} 的 {mode} 是 {status}（期望 fail，错误码 {spec['code']}）")
            if spec["code"] not in error:
                problems.append(
                    f"负向错误码不对：{backend} 的 {mode} 期望 {spec['code']}，实际 error={error}")
            if spec["text"] not in error:
                problems.append(
                    f"{mode} 的 error 里缺「{spec['text']}」：{backend} 的实际 error={error}")
            for required in BIND_REQUIRED_TEXTS:
                if required not in error:
                    problems.append(
                        f"{mode} 没有证明提交前拒绝：{backend} 的 error 里缺「{required}」，"
                        f"实际 error={error}")
            lines.append(f"{backend:<7} {mode:<18} status={status} code={spec['code']}")

    summary = "\n".join(lines)
    print("C# 纹理采样证据对账")
    print(summary)
    print(f"问题 {len(problems)} 处")
    for problem in problems:
        print("  ✗ " + problem)

    log_path = report_dir / "check_csharp_texture_sampling.log"
    try:
        log_path.write_text(
            "C# 纹理采样证据对账（报告目录 " + str(report_dir) + "）\n" + summary +
            "\n问题 " + str(len(problems)) + " 处\n" +
            "\n".join("  ✗ " + problem for problem in problems) + "\n",
            encoding="utf-8")
    except OSError as exc:
        print(f"写不了对账日志 {log_path}：{exc}", file=sys.stderr)
        return 2

    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
