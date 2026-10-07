"""解析期垫片形状探针 —— 量 `Tools/CodeGen/clang_shim/cpuid.h` 的三种写法各自贡献几条解析错误，
并复跑"四种隔离上下文"（本 change 的 design 里声称试过、当时却没有脚本支撑）。

用法（在仓库根执行）：

    python openspec/changes/clear-codegen-parse-blockers/evidence/probe_shim_shape.py

两部分：

1. **三种垫片形状 × AVX2 开/关**：探针头照抄 Windows SDK `DirectXMathMisc.inl` 里
   `XMVerifyCPUSupport` 的两处调用形状（含 `defined(__cpuid)` 的分支），标记带 `serializable`，
   所以"解析干净"的读数就是退出码 0。
2. **四种隔离上下文**（都用现状垫片、都不开 AVX2）：先 include `<intrin.h>` / 先 include `<Windows.h>` /
   先 include `<DirectXMath.h>` / 先调用 `__cpuid` 再 include 垫片 —— 用来复现（或否认）那条
   `definition of builtin function '__cpuid'`。

只往 `%TEMP%` 写文件；脚本开头关掉了字节码缓存写入。
"""

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[4]
# 由 main() 从 build.py 读出来填（`CODEGEN_EXTRA_DEFINES`：工具解析时额外加的 -D）
EXTRA_DEFINES: list = []

# 探针头：照抄 DirectXMathMisc.inl 里 XMVerifyCPUSupport 的两处调用形状。
PROBE_HEADER = r'''#pragma once
#include <CodeGenMeta.h>
#include <cpuid.h>

inline bool ProbeCpuSupport() noexcept
{
    int CPUInfo[4] = { -1 };
#if (defined(__clang__) || defined(__GNUC__)) && defined(__cpuid)
    __cpuid(0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
#else
    __cpuid(CPUInfo, 0);
#endif
#if defined(__AVX2__) || defined(_XM_AVX2_INTRINSICS_)
#if defined(__clang__) || defined(__GNUC__)
    __cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
#else
    __cpuidex(CPUInfo, 7, 0);
#endif
#endif
    return CPUInfo[0] >= 1;
}

CA_REFLECT(serializable)
struct CpuProbe { int a; };
'''

MACRO_SHIM = (
    "#pragma once\n"
    "// GCC 形状的名字用宏提供（clang 自带 __cpuid / __cpuidex 内建，不许再定义它们）。\n"
    "#define __cpuid_count(level, count, a, b, c, d) do { int r_[4]; __cpuidex(r_, (level), (count)); "
    "(a)=r_[0]; (b)=r_[1]; (c)=r_[2]; (d)=r_[3]; } while (0)\n"
)

# 四种隔离上下文：前缀写在标记之前，用来复现（或否认）那个"内建被重定义"的诊断。
CONTEXTS = [
    ("先 include <intrin.h>", '#include <intrin.h>\n'),
    ("先 include <Windows.h>", '#include <Windows.h>\n'),
    ("先 include <DirectXMath.h>", '#include <DirectXMath.h>\n'),
    ("先用 __cpuid 再 include 垫片",
     '#include <CodeGenMeta.h>\ninline int EarlyUse() { int r[4] = {0}; __cpuid(r, 0); return r[0]; }\n'),
]


def variants(tool_dir: Path):
    return {
        "current": (REPO / "Tools" / "CodeGen" / "clang_shim" / "cpuid.h").read_text(encoding="utf-8"),
        "nodef": "#pragma once\n// 什么都不定义：只让 include 过得去\n",
        "macro": MACRO_SHIM,
    }


def run_one(tool, env, case_dir: Path, out_dir: Path, extra_defines):
    out_dir.mkdir(exist_ok=True)
    cmd = [str(tool), str(case_dir / "CpuProbe.h"), "CpuProbe", f"-I{case_dir}",
           "-ICACore/header", *EXTRA_DEFINES, *env, *extra_defines,
           "--emit-all", "--out-dir", str(out_dir)]
    proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    return proc


def main():
    spec = importlib.util.spec_from_file_location("build_script", REPO / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    global EXTRA_DEFINES
    EXTRA_DEFINES = list(getattr(build, "CODEGEN_EXTRA_DEFINES", []))
    cc_file = REPO / "out" / "build" / "x64-debug" / "compile_commands.json"
    if not cc_file.exists():
        print(f"缺少 {cc_file}：先跑 `python build.py --config Debug` 生成它")
        return 2
    env = build._compile_env_by_root(json.loads(cc_file.read_text(encoding="utf-8")))["D3D12RenderBackend"]
    tool = REPO / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"
    base = Path(tempfile.mkdtemp(prefix="codegen_shim_shape_"))
    failed = []

    print("=" * 78)
    print("第一部分：三种垫片形状 × AVX2 开/关")
    for name, shim_text in variants(base).items():
        case_dir = base / name
        case_dir.mkdir(parents=True, exist_ok=True)
        (case_dir / "cpuid.h").write_text(shim_text, encoding="utf-8")
        (case_dir / "CpuProbe.h").write_text(PROBE_HEADER, encoding="utf-8")
        for avx2 in (False, True):
            proc = run_one(tool, env, case_dir, case_dir / ("out_avx2" if avx2 else "out"),
                           ["-D_XM_AVX2_INTRINSICS_"] if avx2 else [])
            label = "AVX2 开" if avx2 else "AVX2 关"
            print("-" * 78)
            print(f"垫片写法 {name:8s}  {label}  exit={proc.returncode}")
            for line in (proc.stderr or "").strip().splitlines():
                print(f"    | {line}")
            if name == "macro" and proc.returncode != 0:
                failed.append(f"{name}/{label}")

    print("=" * 78)
    print("第二部分：四种隔离上下文（现状垫片、AVX2 关）—— 看那条 definition of builtin 会不会出现")
    for index, (label, prefix) in enumerate(CONTEXTS):
        case_dir = base / f"ctx{index}"
        case_dir.mkdir(parents=True, exist_ok=True)
        (case_dir / "cpuid.h").write_text(variants(base)["current"], encoding="utf-8")
        # 前缀放在最前面，标记仍然是定义过的（不然工具会走"空标注"分支，只报条数、不报内容）。
        (case_dir / "CpuProbe.h").write_text(prefix + PROBE_HEADER, encoding="utf-8")
        proc = run_one(tool, env, case_dir, case_dir / "out", [])
        hit = "definition of builtin function" in (proc.stderr or "")
        print("-" * 78)
        print(f"{label}  exit={proc.returncode}  出现 definition of builtin function：{'是' if hit else '否'}")
        for line in (proc.stderr or "").strip().splitlines():
            print(f"    | {line}")

    print("=" * 78)
    print(f"产物目录：{base}")
    print(f"宏版垫片两处开关都应为 exit 0；不符合的：{failed if failed else '（无）'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
