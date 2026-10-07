"""把一个头"临时插一行标记"再解析，逼工具打印它自己的诊断原文。

为什么需要它：工具的"空标注"那条提前返回只打印诊断**条数**，不打印内容 —— 而"改完那几条错误有没有消失"
正是本 change 的判据。给头里的目标类型补一行 `CA_REFLECT(serializable)`，工具就会走进会逐条打印诊断的
保真门分支。注意：**改的是 `%TEMP%` 里的副本，仓库里的文件一个字节都不动**。

用法（在仓库根执行）：

    python openspec/changes/clear-codegen-parse-blockers/evidence/probe_header_diagnostics.py \
        Interface/RenderInterface/header/GPUGraph.h ViewRectData Interface/RenderInterface "struct ViewRectData"

最后一个参数是要在它**前面**插标记的那一行（按 lstrip 前缀匹配）。省略时插在文件开头（多半绑不上类型，
只是让工具走另一条分支）。

**打印的是完整 stderr**（不截断）；脚本开头关掉了字节码缓存写入，不会在仓库里留下 `__pycache__`。
"""

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description="打印某个头在保真门下的诊断原文")
    parser.add_argument("header", help="仓库相对路径，例如 Interface/RenderInterface/header/GPUGraph.h")
    parser.add_argument("type_name", help="要标注的类型名")
    parser.add_argument("owner", help="归属模块根，例如 Interface/RenderInterface")
    parser.add_argument("marker_before", nargs="?", default="",
                        help="在这一行之前插 CA_REFLECT(serializable)，按 lstrip 前缀匹配")
    parser.add_argument("--error-limit1", action="store_true",
                        help="额外给工具传 -ferror-limit=1：链上任何一条错误都会升级成致命诊断，"
                             "于是保真门被触发、诊断原文会被打印出来（用它看链上还剩什么）")
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location("build_script", REPO / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    cc_file = REPO / "out" / "build" / "x64-debug" / "compile_commands.json"
    if not cc_file.exists():
        print(f"缺少 {cc_file}：先跑 `python build.py --config Debug` 生成它")
        return 2
    envs = build._compile_env_by_root(json.loads(cc_file.read_text(encoding="utf-8")))
    env = envs.get(args.owner)
    if not env:
        print(f"模块 '{args.owner}' 没有推导出编译环境")
        return 2

    source = (REPO / args.header).read_text(encoding="utf-8")
    lines, done, include_added = [], False, False
    for line in source.splitlines(keepends=True):
        if not include_added and line.startswith("#include"):
            lines.append("#include <CodeGenMeta.h>\n")
            include_added = True
        if not done and args.marker_before and line.lstrip().startswith(args.marker_before):
            lines.append("CA_REFLECT(serializable)\n")
            done = True
        lines.append(line)
    if not done:
        lines.insert(0, "CA_REFLECT(serializable)\n")

    case_dir = Path(tempfile.mkdtemp(prefix="codegen_header_diag_"))
    copy_path = case_dir / Path(args.header).name
    copy_path.write_text("".join(lines), encoding="utf-8")
    out_dir = case_dir / "out"
    out_dir.mkdir()
    cmd = [str(REPO / "out" / "build" / "x64-debug" / "bin" / "CodeGen.exe"),
           str(copy_path), args.type_name, *env,
           *[f"-I{p}" for p in build.CODEGEN_EXTRA_INCLUDES],
           *list(getattr(build, "CODEGEN_EXTRA_DEFINES", [])),
           f"-I{REPO / Path(args.header).parent}",
           *(["-ferror-limit=1"] if args.error_limit1 else []),
           "--emit-all", "--out-dir", str(out_dir)]
    proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    print(f"标记插在：{'找到 ' + args.marker_before if done else '文件开头（没匹配到目标行）'}")
    print(f"副本：{copy_path}")
    print(f"退出码：{proc.returncode}")
    print("---- stderr ----")
    print((proc.stderr or "").strip().replace(str(case_dir), "<tmp>"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
