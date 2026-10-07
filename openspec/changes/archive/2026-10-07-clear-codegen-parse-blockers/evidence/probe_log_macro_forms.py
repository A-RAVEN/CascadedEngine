"""日志/断言宏的两种调用形态探针 —— 任务 2.2 的判据。

它把五种形态各写一行（字面量 / 运行期 `castl::string_view` / 运行期 `const char*` / `CA_LOG` 无参字面量 /
`CA_LOG` 带参数），配一个带标注的探针类型，用 `build.py` 为 `CACore` 推导出的环境跑
`CodeGen.exe --emit-all`：

* 字面量形态与 `CA_ASSERT("文字")` 的展开同形 —— 改之前 clang 会报
  `call to 'error_with_location' is ambiguous`；
* 运行期形态取自仓库里真实的调用点（`ShaderCompilerSlang/private/Compiler.cpp` 那类
  `CA_LOG_ERR((const char*)ptr)`），这些路径**必须保留**，不许为了消歧义删掉它们或改签名；
* 形态四/五是**同一对重载模式的第二处**（`log_with_location` 也有 `format_string` 与 `castl::string_view`
  两个同形重载），一起量，避免同一根因修一半。

用法（在仓库根执行）：

    python openspec/changes/clear-codegen-parse-blockers/evidence/probe_log_macro_forms.py

判据：退出码 0 且诊断里 `ambiguous` 出现 **0** 次。只往 `%TEMP%` 写文件。
"""

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[4]

PROBE_HEADER = r'''#pragma once
#include <DebugUtils.h>
#include <HashKeys.h>
#include <CodeGenMeta.h>

// 形态一：字符串字面量（与 CA_ASSERT(cond, "文字") / CA_LOG_ERR("文字") 的展开同形）
inline void ProbeLiteralForm()
{
    cacore::error_with_location(std::source_location::current(), "Invalid Compute Dispatch!");
}

// 形态二：运行期字符串（仓库里真实存在：Compiler.cpp 的 CA_LOG_ERR((const char*)ptr)）
inline void ProbeRuntimeForm(castl::string_view const& message)
{
    cacore::error_with_location(std::source_location::current(), message);
}

// 形态三：运行期 const char*（同上，另一条真实路径）
inline void ProbeRuntimeCharPtrForm(const char* message)
{
    cacore::error_with_location(std::source_location::current(), message);
}

// 形态四：CA_LOG 的无参字面量（同一对重载模式的第二处，见 log_with_location）
inline void ProbeLogLiteralNoArgs()
{
    CA_LOG("plain literal without args");
}

// 形态五：CA_LOG 带参数（格式串 + 实参，必须保持能格式化）
inline void ProbeLogLiteralWithArgs(int value)
{
    CA_LOG("value = {}", value);
}

// 形态六：运行期拼出来的 castl::string（仓库里真实存在：ShaderArgList.h 的
// CA_ASSERT(cond, castl::string("…") + name.string() + "…")）
inline void ProbeBuiltStringForm(cacore::NameHash const& name, uint32_t sizeInBytes)
{
    CA_ASSERT(sizeInBytes > 0, castl::string("shader parameter ") + name.string() + "has an empty value");
}

CA_REFLECT(serializable)
struct LogMacroProbe { int a; };
'''


def main():
    spec = importlib.util.spec_from_file_location("build_script", REPO / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    cc_file = REPO / "out" / "build" / "x64-debug" / "compile_commands.json"
    if not cc_file.exists():
        print(f"缺少 {cc_file}：先跑 `python build.py --config Debug` 生成它")
        return 2
    env = build._compile_env_by_root(json.loads(cc_file.read_text(encoding="utf-8"))).get("CACore")
    if not env:
        print("模块 'CACore' 没有推导出编译环境")
        return 2
    case_dir = Path(tempfile.mkdtemp(prefix="codegen_log_macro_"))
    (case_dir / "LogMacroProbe.h").write_text(PROBE_HEADER, encoding="utf-8")
    out_dir = case_dir / "out"
    out_dir.mkdir()
    cmd = [str(REPO / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"),
           str(case_dir / "LogMacroProbe.h"), "LogMacroProbe", *env,
           *[f"-I{p}" for p in build.CODEGEN_EXTRA_INCLUDES],
           *list(getattr(build, "CODEGEN_EXTRA_DEFINES", [])),
           "--emit-all", "--out-dir", str(out_dir)]
    proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    stderr = (proc.stderr or "").strip()
    ambiguous = stderr.count("ambiguous")
    print(f"退出码：{proc.returncode}   诊断里 `ambiguous` 出现次数：{ambiguous}")
    for line in stderr.splitlines():
        print(f"  | {line}")
    print(f"临时目录：{case_dir}")
    return 0 if (proc.returncode == 0 and ambiguous == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
