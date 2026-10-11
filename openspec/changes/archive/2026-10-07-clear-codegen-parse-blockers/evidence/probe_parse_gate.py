"""解析门红态 / 绿态探针 —— 本 change 的判据，可原样重跑。

用法（在仓库根执行）：

    python openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_parse_gate.py
    python openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_parse_gate.py --expect green

它做三件事：

1. 用 `build.py` 自己推导的编译环境（`compile_commands.json`）跑 `out/build/x64-debug/bin/CodeGen.exe`；
2. 对四个目标类型请求 `--emit-all`，打印退出码、工具报的"主头自身错误级诊断"条数、是否有致命诊断、
   写出的产物份数、以及**完整的** stderr（本 change 动工之前：四个都是 exit 6、零产物）；
3. 跑三个对照（两个已经在接入清单里的行、一个 D3D12 模块自己的头），证明"环境本身是好的"，
   被拒不是缺 include 那一类问题。

**绿态判据不只看退出码**：`--expect green` 要求四个目标类型**退出码为 0 且各写出一份产物**
（`merge-codegen-products` 之后一个类型只有一份 `<类型名>.codegen.h`；在那之前是三份），
并且 stderr 里不出现 `no annotated types` —— 工具在"这个头里一条标注都没有"时也会 exit 0 且一份产物都不写，
只看退出码会把"标注丢了"判成通过。

只往 `%TEMP%` 写产物；脚本开头关掉了字节码缓存写入，所以不会在仓库里留下 `__pycache__`。
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

# 归档把这个文件往下挪了一层（`openspec/changes/archive/<日期>-<change>/evidence/`），所以仓库根是
# parents[5]；归档之前它在 `openspec/changes/<change>/evidence/` 下，那时是 parents[4]。
REPO = Path(__file__).resolve().parents[5]
CONFIG_DIR = "x64-debug"
# 一个类型一份产物（`<类型名>.codegen.h`，反射 / 序列化 / 哈希三节）。合并之前这里是 3：三份产物。
PRODUCTS_EXPECTED = 1

# (归属模块, 头文件（仓库相对路径）, 类型名)
TARGETS = [
    ("Interface/RenderInterface", "Interface/RenderInterface/header/GPUGraph.h",
     "ViewRectData"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h",
     "GPUPipelineStateKey"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h",
     "VertexInputBindingData"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h",
     "InputElementDesc"),
]

# 对照：预期"解析干净"的头。注意它们不产出这一份产物（要么出一份、要么是空名单），所以单独一组判定。
CONTROLS = [
    ("Interface/RenderInterface", "Interface/RenderInterface/header/ShaderResourceHandle.h",
     "ResourceHandleKeyData"),
    ("Interface/RenderInterface", "Interface/RenderInterface/header/CPipelineStateObject.h",
     "RectSate"),
    ("D3D12RenderBackend", "D3D12RenderBackend/private/D3D12Includes.h",
     "D3D12Probe"),
]

IN_HEADER = re.compile(r"^\s*(\d+) error-level diagnostic\(s\) inside the parsed header itself",
                       re.MULTILINE)
FATAL = "unresolved include (fatal)"


def run_case(tool, env, extra, rel_header, type_name, out_root, index):
    out_dir = out_root / f"{index:02d}_{type_name}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(tool), rel_header, type_name, *env, *extra, "--emit-all", "--out-dir", str(out_dir)]
    proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    stderr = (proc.stderr or "").strip()
    wrote = len([l for l in (proc.stdout or "").splitlines() if l.startswith("CodeGen wrote:")])
    match = IN_HEADER.search(stderr)
    return {
        "exit": proc.returncode,
        "in_header": int(match.group(1)) if match else 0,
        "fatal": FATAL in stderr,
        "wrote": wrote,
        "empty_whitelist": "no annotated types" in stderr,
        "stderr": stderr,
    }


def green_problem(result):
    """绿态下这一条为什么还不算通过；通过则返回空串。"""
    if result["exit"] != 0:
        return f"退出码 {result['exit']}"
    if result["empty_whitelist"]:
        return "工具报 no annotated types（标注丢了，一份产物都没写）"
    if result["wrote"] < PRODUCTS_EXPECTED:
        return f"只写出 {result['wrote']} 份产物（应为 {PRODUCTS_EXPECTED} 份）"
    if result["in_header"] or result["fatal"]:
        return "仍有错误级诊断或致命诊断"
    return ""


def main():
    parser = argparse.ArgumentParser(description="解析门探针")
    parser.add_argument("--expect", choices=["red", "green"], default="red",
                        help="red = 期望四个目标类型被拒（动工之前）；green = 期望四个都 exit 0 且各出一份产物（修完之后）")
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location("build_script", REPO / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)

    cc_file = REPO / "out" / "build" / CONFIG_DIR / "compile_commands.json"
    if not cc_file.exists():
        print(f"缺少 {cc_file}：先跑 `python build.py --config Debug` 生成它")
        return 2
    entries = json.loads(cc_file.read_text(encoding="utf-8"))
    envs = build._compile_env_by_root(entries)
    tool = REPO / "out" / "build" / CONFIG_DIR / "bin" / "CodeGen.exe"
    extra = [f"-I{p}" for p in build.CODEGEN_EXTRA_INCLUDES] + list(
        getattr(build, "CODEGEN_EXTRA_DEFINES", []))
    out_root = Path(tempfile.mkdtemp(prefix="codegen_parse_gate_"))

    # 两组分开存：不许按位置切片，否则某条被跳过时判定会错位到对照身上。
    results = {"target": [], "control": []}
    skipped = []
    for group, cases in (("target", TARGETS), ("control", CONTROLS)):
        print("=" * 78)
        print("目标类型（改之前应当被拒）" if group == "target" else "对照（应当解析干净）")
        for index, (owner, rel_header, type_name) in enumerate(cases):
            env = envs.get(owner)
            if not env:
                print(f"  {type_name}: 模块 '{owner}' 没有推导出编译环境 —— 记为未跑，不算通过")
                skipped.append(type_name)
                continue
            result = run_case(tool, env, extra, rel_header, type_name, out_root, index)
            results[group].append((type_name, result))
            print("-" * 78)
            print(f"  {type_name}  exit={result['exit']}  主头诊断={result['in_header']}  "
                  f"致命={result['fatal']}  产物={result['wrote']}")
            print(f"    头: {rel_header}")
            for line in result["stderr"].splitlines():
                print(f"    | {line}")
    print("=" * 78)
    print(f"产物目录：{out_root}")

    if args.expect == "green":
        bad = [name for name, r in results["target"] if green_problem(r)]
    else:
        bad = [name for name, r in results["target"] if r["exit"] == 0]
    ran = {name for name, _ in results["target"]}
    bad += [f"{t[2]}（没跑：缺编译环境）" for t in TARGETS if t[2] not in ran]
    print(f"期望={args.expect}；不符合期望的：{bad if bad else '（无）'}"
          f"；跑了 {len(results['target'])}/{len(TARGETS)} 个目标、{len(results['control'])}/{len(CONTROLS)} 个对照"
          f"；跳过 {len(skipped)} 个")
    control_noise = [name for name, r in results["control"] if r["fatal"] or r["in_header"]]
    if control_noise:
        print(f"⚠ 对照里出现诊断（说明环境本身有问题）：{control_noise}")
    return 1 if (bad or skipped or control_noise) else 0


if __name__ == "__main__":
    sys.exit(main())
