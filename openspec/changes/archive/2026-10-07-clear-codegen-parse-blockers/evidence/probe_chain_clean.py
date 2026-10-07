"""链上错误有没有清零 —— 用 `-ferror-limit=1` 把"链上任何一条错误级诊断"变成可观测的拒绝。

为什么需要它：工具的保真门判据逐字是
`bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }`
（`Tools/CodeGen/CodeGenMain.cpp`）—— 主头自身有错误、或有致命诊断才拒收；include 链里的错误单独不算，
而且诊断只在门被触发时才打印。所以"退出码 0"分不清两种情况：链上真干净，还是链上还剩几条错误
（少于 clang 的错误上限）没被打印。把上限压到 1 之后，链上**任何一条**错误都会升级成致命诊断，
工具就会把它打印出来 —— 于是"清零"变成可观测的读数。

用法（在仓库根执行）：

    python openspec/changes/clear-codegen-parse-blockers/evidence/probe_chain_clean.py
    python openspec/changes/clear-codegen-parse-blockers/evidence/probe_chain_clean.py --expect clean

`--expect clean` 时，任何一条目标头只要仍打印错误级诊断或致命诊断，就以非零退出收场。
只往 `%TEMP%` 写产物；脚本开头关掉了字节码缓存写入。
"""

import argparse
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[4]
ERROR_LINE = re.compile(r"error:", re.IGNORECASE)

# (归属模块, 头, 目标类型)：四个待迁类型所在的两个头，外加三个对照头。
CASES = [
    ("Interface/RenderInterface", "Interface/RenderInterface/header/GPUGraph.h", "ViewRectData"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h",
     "GPUPipelineStateKey"),
    ("Interface/RenderInterface", "Interface/RenderInterface/header/CPipelineStateObject.h", "RectSate"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderLibrary"),
]


def main():
    parser = argparse.ArgumentParser(description="链上错误清零探针")
    parser.add_argument("--expect", choices=["clean", "dirty"], default="dirty",
                        help="clean = 期望一个错误级诊断都没有（修完之后）；dirty = 只打印读数（动工之前）")
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location("build_script", REPO / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    cc_file = REPO / "out" / "build" / "x64-debug" / "compile_commands.json"
    if not cc_file.exists():
        print(f"缺少 {cc_file}：先跑 `python build.py --config Debug` 生成它")
        return 2
    envs = build._compile_env_by_root(json.loads(cc_file.read_text(encoding="utf-8")))
    extra = [f"-I{p}" for p in build.CODEGEN_EXTRA_INCLUDES] + list(
        getattr(build, "CODEGEN_EXTRA_DEFINES", []))
    tool = REPO / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"
    out_root = Path(tempfile.mkdtemp(prefix="codegen_chain_clean_"))

    dirty = []
    for index, (owner, rel_header, type_name) in enumerate(CASES):
        env = envs.get(owner)
        out_dir = out_root / f"{index:02d}_{type_name}"
        out_dir.mkdir(parents=True, exist_ok=True)
        if not env:
            print(f"{type_name}: 模块 '{owner}' 没有推导出编译环境 —— 记为未跑")
            dirty.append(f"{type_name}（没跑）")
            continue
        cmd = [str(tool), rel_header, type_name, *env, *extra, "-ferror-limit=1",
               "--emit-all", "--out-dir", str(out_dir)]
        proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        stderr = (proc.stderr or "").strip()
        errors = [l for l in stderr.splitlines() if ERROR_LINE.search(l)]
        print("=" * 78)
        print(f"{type_name}  ({rel_header})  exit={proc.returncode}  错误级诊断行数={len(errors)}")
        for line in stderr.splitlines():
            print(f"  | {line}")
        if errors or proc.returncode == 6:
            dirty.append(type_name)
    print("=" * 78)
    print(f"产物目录：{out_root}")
    print(f"期望={args.expect}；链上仍有错误的：{dirty if dirty else '（无）'}")
    if args.expect == "clean" and dirty:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
