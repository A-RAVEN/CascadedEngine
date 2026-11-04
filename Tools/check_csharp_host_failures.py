#!/usr/bin/env python3
"""C# 宿主的失败路径测试（docs/testing.md 的第 ③ 层：进程级）。

五条场景，每条都断言"退出码 + 输出里的可核子串"：

  1. 缺托管程序集          → 退出码 3，输出含 Scripting.Managed.dll 与 找过的路径
  2. 配置路径不存在        → 退出码 3，输出含该路径与 hostfxr.dll
  3. 配置要的运行时版本没有 → 退出码 3，输出含 7.0.0 与 本机已装的运行时版本
  4. 函数表 ABI 不符        → 退出码 3，输出含 ABI 版本不符 与 托管侧期望 8（自检开关构造：
                              引擎侧故意把当前 v8 加成 9，托管侧仍期望 8）
  5. 托管方法抛异常        → 退出码 1（GPUBackendTester 对失败的当前操作按既有约定返回 1），
                              输出含 Tick 抛异常 与 实际 4，且不产生新 crash_*.dmp（自检开关构造）

为什么第 5 条期望 1 而不是 0：这一条验证的是"受控失败、进程不崩"——托管 Tick 抛异常必须让
当前测试操作失败（tester 退出码 1），同时又必须保留错误码 4/异常文本且不产生崩溃转储。
把受控失败要求成无条件退出 0 会让这条判据恒绿（csharp-render-backend-triangle 任务 5.1 / 6.8）。
为什么在这一层：这些情形都是"一启动就失败"，进程里没有机会断言，只能从外面看退出码与输出。
为什么用自检开关：第 4、5 条原本要临时改源码才能构造，改开关之后脚本可以反复跑、跑完不脏工作区。

退出码：
  0 = 五条全过
  1 = 有场景不符（逐条打印期望与实际）
  2 = 判据自身失效（找不到 GPUBackendTester.exe / 产物 / runtimeconfig）

用法：
    python Tools/check_csharp_host_failures.py
    python Tools/check_csharp_host_failures.py --list
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

BIN_SUBDIR = Path("out") / "build" / "x64-debug" / "bin"
TESTER = "GPUBackendTester.exe"
MANAGED_DLL = "Scripting.Managed.dll"
MANAGED_CONFIG = "Scripting.Managed.runtimeconfig.json"


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "openspec").is_dir() and (candidate / "CACore").is_dir():
            return candidate
    return start


REPO = find_repo_root(Path(__file__).resolve())


def run_tester(binary: Path, extra_env: dict | None = None) -> tuple[int, str]:
    """跑一次测试宿主，返回（退出码，stdout+stderr 合并文本）。"""
    env = dict(os.environ)
    env.pop("CASCADED_CSHARP_SELFTEST", None)
    env.pop("CASCADED_CSHARP_RUNTIMECONFIG", None)
    if extra_env:
        env.update(extra_env)
    completed = subprocess.run(
        [str(binary), "--backend", "vulkan", "--headless", "1"],
        cwd=str(REPO),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=300,
    )
    return completed.returncode, completed.stdout.decode("utf-8", "replace")


def count_dumps() -> int:
    return len(list(REPO.glob("crash_*.dmp")))


def clear_test_logs() -> None:
    for log in (REPO / "test_output").glob("*.log"):
        log.unlink(missing_ok=True)


def collect_test_logs() -> str:
    """headless 模式会把每个测试的 stdout freopen 到 test_output/<name>.log，
    所以进程级捕获到的输出里没有那些行 —— 断言前要把这些日志一起读进来。"""
    parts = []
    for log in sorted((REPO / "test_output").glob("*.log")):
        parts.append(log.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def check_scenarios(binary: Path, bin_dir: Path) -> list[tuple[str, bool, str]]:
    results: list[tuple[str, bool, str]] = []
    work_dir = Path(tempfile.mkdtemp(prefix="csharp-host-failures-"))

    def record(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    try:
        # 1. 缺托管程序集：改名再还原
        moved = bin_dir / (MANAGED_DLL + ".moved")
        dll = bin_dir / MANAGED_DLL
        try:
            if not dll.is_file():
                raise RuntimeError(f"找不到 {dll}")
            dll.replace(moved)
            code, text = run_tester(binary)
            ok = code == 3 and MANAGED_DLL in text and "找过的路径" in text
            record("缺托管程序集", ok, f"退出码 {code}（期望 3）；含 dll 名与路径清单："
                                      f"{MANAGED_DLL in text}/{'找过的路径' in text}")
        finally:
            if moved.is_file():
                moved.replace(dll)

        # 2. 配置路径不存在
        missing = work_dir / "no_such_dir" / "bad.runtimeconfig.json"
        code, text = run_tester(binary, {"CASCADED_CSHARP_RUNTIMECONFIG": str(missing)})
        ok = code == 3 and "bad.runtimeconfig.json" in text and "hostfxr.dll" in text
        record("配置路径不存在", ok, f"退出码 {code}（期望 3）；含该路径与 hostfxr.dll："
                                    f"{'bad.runtimeconfig.json' in text}/{'hostfxr.dll' in text}")

        # 3. 配置要的运行时版本本机没有
        real_config = bin_dir / MANAGED_CONFIG
        if not real_config.is_file():
            raise RuntimeError(f"找不到 {real_config}")
        patched = work_dir / "v7.runtimeconfig.json"
        content = real_config.read_text(encoding="utf-8")
        content = re.sub(r'"version"\s*:\s*"[^"]+"', '"version": "7.0.0"', content, count=1)
        patched.write_text(content, encoding="utf-8")
        code, text = run_tester(binary, {"CASCADED_CSHARP_RUNTIMECONFIG": str(patched)})
        ok = code == 3 and "7.0.0" in text and "本机已装的运行时版本" in text
        record("运行时版本无匹配", ok, f"退出码 {code}（期望 3）；含 7.0.0 与版本清单："
                                      f"{'7.0.0' in text}/{'本机已装的运行时版本' in text}")

        # 4. 函数表 ABI 不符（自检开关）：当前手写 ABI 已升到 v8（csharp-render-texture-sampling 在
        #    EngineApi 末尾追加纹理接口编号）。自检把引擎侧改成 9，托管侧仍期望 8 —— 断言必须
        #    点名**当前** ABI 8，不能再期待上一版（v7）或更早的版本 2。
        code, text = run_tester(binary, {"CASCADED_CSHARP_SELFTEST": "abi-mismatch"})
        abi_named_current = "ABI 版本不符" in text and "托管侧期望 8" in text
        ok = code == 3 and abi_named_current
        record("ABI 版本不符", ok, f"退出码 {code}（期望 3）；含 ABI 版本不符 与 托管侧期望 8："
                                  f"{abi_named_current}")

        # 5. 托管方法抛异常（自检开关）：受控失败 = tester 对失败的当前操作返回 1，但进程不崩、
        #    错误码 4 与异常文本出现在输出里、不产生新崩溃转储。
        dumps_before = count_dumps()
        clear_test_logs()
        code, text = run_tester(binary, {"CASCADED_CSHARP_SELFTEST": "throw"})
        text += "\n" + collect_test_logs()
        new_dumps = count_dumps() - dumps_before
        ok = code == 1 and "Tick 抛异常" in text and "实际 4" in text and new_dumps == 0
        record("托管抛异常", ok, f"退出码 {code}（期望 1 = 受控失败）；含异常文本与错误码 4："
                                f"{'Tick 抛异常' in text}/{'实际 4' in text}；新 dump {new_dumps} 个")
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="C# 宿主的五条失败路径检查（层 ③）")
    parser.add_argument("--list", action="store_true", help="只列出要跑的场景")
    args = parser.parse_args()

    scenarios = ["缺托管程序集", "配置路径不存在", "运行时版本无匹配", "ABI 版本不符", "托管抛异常"]
    if args.list:
        for name in scenarios:
            print("  " + name)
        return 0

    bin_dir = REPO / BIN_SUBDIR
    binary = bin_dir / TESTER
    if not binary.is_file():
        print(f"找不到 {binary} —— 先跑 python build.py --config Debug（判据自身失效）", file=sys.stderr)
        return 2
    for required in (MANAGED_DLL, MANAGED_CONFIG):
        if not (bin_dir / required).is_file():
            print(f"找不到 {bin_dir / required} —— 托管侧产物不在（判据自身失效）", file=sys.stderr)
            return 2

    try:
        results = check_scenarios(binary, bin_dir)
    except Exception as exc:  # noqa: BLE001 — 场景构造失败也算判据自身失效，不当成通过
        print(f"场景构造失败：{type(exc).__name__}: {exc}", file=sys.stderr)
        return 2

    failed = 0
    for name, ok, detail in results:
        print(("[通过] " if ok else "[失败] ") + name + "：" + detail)
        if not ok:
            failed += 1

    print(f"\n{len(results)} 个场景：{len(results) - failed} 通过、{failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
