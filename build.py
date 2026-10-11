#!/usr/bin/env python3
"""CascadedEngine build script with automatic MSVC environment setup.

Usage:
    python build.py                 # configure + build (all targets, RelWithDebInfo)
    python build.py --no-configure  # build only (skip cmake configure)
    python build.py --config Debug  # use the Debug configuration (validation layers enabled)
    python build.py TargetName      # build a specific target
    python build.py --offline-fetch # configure 时不去碰依赖的远端（本机连不上 github 时用）

`--offline-fetch` 做两件事：给 configure 传 `FETCHCONTENT_UPDATES_DISCONNECTED=ON`（跳过每个依赖的
update 步 —— 那一步是 `git fetch`，本机连不上 github 时会以
`fatal: unable to access 'https://github.com/…': Could not connect to server` 失败），并把
`FETCHCONTENT_SOURCE_DIR_STB` 指向已经拉好的 `<构建目录>/_deps/stb-src`（`stb` 用的是 `GIT_TAG master`，
只有它每次 configure 都要联网）。不带这个开关时行为与从前完全一样。

生成物只落构建目录（`out/build/<预设>/codegen/<模块>/`），源码树里没有生成代码：既没有"写回仓库"的开关，
也没有"与入库副本逐字节对拍"这一步。
"""

import os
import sys
import subprocess
import json
import re
import shlex
import shutil
import hashlib
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROJECT_DIR = Path(__file__).parent.resolve()

# The build log is read back and printed verbatim (run_msvc). Compiler diagnostics and static_assert
# messages can contain non-ASCII text, and the default console encoding on Windows (cp1252 / cp936)
# cannot represent it: `print()` then raised UnicodeEncodeError INSIDE the error-reporting path, so the
# real compile error was replaced by a Python traceback and the log file stayed behind on disk.
# Measured 2026-10-03: a failing CoreTests compile printed
#   UnicodeEncodeError: 'charmap' codec can't encode characters in position 2150-2169
# instead of the C2248 diagnostic. Force UTF-8 on both streams; characters the console still cannot
# represent are replaced rather than raising.
for _stream in (sys.stdout, sys.stderr):
    _stream.reconfigure(encoding="utf-8", errors="replace")

# config name -> CMake preset name (preset sets binaryDir to out/build/<preset>)
CONFIGS = {
    "RelWithDebInfo": "x64-relWithDebugInfo",
    "Debug": "x64-debug",
}
DEFAULT_CONFIG = "RelWithDebInfo"

BUILD_DIR: Path | None = None
NINJA_FILE: Path | None = None

# Codegen manifest: header (relative to PROJECT_DIR, the file the sidecar parses), the type name, and
# the CMake module root that owns it (used to pick that module's compile env from compile_commands.json).
# The manifest points at the self-contained fixture under Tools/CodeGen/ so the build-time path still has a
# subject without coupling to engine headers. 产物落构建目录、并且真的被编译单元消费（CoreTests 里断言它），
# 所以这一行查的是真东西 —— 不是生成一份没人读的文件。
CODEGEN_ANNOTATED = [
    # (header, type, owner directory): 归属目录是**真正编译（消费）这个类型**的模块，它同时决定两件事：
    # 用谁的编译环境解析（见 _env_for_root），以及产物落在构建目录的 `codegen/<归属目录，/ 换成 _>/`。
    # 两个自包含夹具的头文件放在 Tools/CodeGen/ 下，但产物是被 Test/CoreTests 的编译单元 include 的，
    # 所以归属写 Test/CoreTests。写"头文件所在的目录"会推不出环境 —— 生成器已经是独立工程，引擎的
    # compile_commands.json 里没有它的编译单元，那样只能靠一张手写的借用表，而基线规格禁止手写清单。
    ("Tools/CodeGen/TestEmit.h", "EmitSample", "Test/CoreTests"),
    # The classification fixture (typedef vs keyword spellings).
    ("Tools/CodeGen/TestKinds.h", "Kinds", "Test/CoreTests"),
    # 私有成员 + 类内友元入口（`CA_CODEGEN_FRIEND`）：出码要成功，而且消费单元里要能编译 ——
    # 后者才证明那四条 friend 声明真的把访问权开了。
    ("Tools/CodeGen/TestPrivate.h", "FriendLike", "Test/CoreTests"),
    # 8.7 的 TResource 对拍夹具：这个头 include `CAResource/IResource.h`，所以它依赖 CoreTests 那条
    # `CAGeneralReourceSystem_Interface`（给 Test/CoreTests 的环境带来 `GeneralResources/header`）。
    ("Tools/CodeGen/TestResource.h", "NewRes", "Test/CoreTests"),
    # The container fixture, consumed by a real engine target
    # (Interface/RenderInterface/private/TestContainerAssertions.cpp).
    ("Interface/RenderInterface/private/TestContainer.h", "Containers", "Interface/RenderInterface"),
    ("Interface/RenderInterface/private/TestContainer.h", "HasFloat", "Interface/RenderInterface"),
    # 容器里装结构体（结构体内部还有无序容器）——撤除退出码 12 那条拒绝的指称对象。
    ("Interface/RenderInterface/private/TestContainer.h", "Inner", "Interface/RenderInterface"),
    ("Interface/RenderInterface/private/TestContainer.h", "Outer", "Interface/RenderInterface"),
    # 定长数组（castl::array / C 数组）：读取路径按位置覆写，不调用 clear()。
    ("Interface/RenderInterface/private/TestContainer.h", "FixedArrays", "Interface/RenderInterface"),
    # CPipelineStateObject 一族（含嵌套类型 StencilStates 与 castl::array 成员）。
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "RasterizerStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "DepthStencilStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "StencilStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "SingleColorAttachmentBlendStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "ColorAttachmentsBlendStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "CPipelineStateObject", "Interface/RenderInterface"),
    # 顶点输入描述：它们当哈希键用（`HashObj<VertexInputsDescriptor>` 是 D3D12 GPUGraph 那几个映射的键），
    # 换哈希驱动时是过不了新驱动的两个宿主之一（另一个是 `ResourceHandleKeyData`），所以要生成特化。
    ("Interface/RenderInterface/header/CVertexInputDescriptor.h", "VertexAttribute", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CVertexInputDescriptor.h", "VertexInputsDescriptor", "Interface/RenderInterface"),
    # 句柄的键（值语义；两个句柄类自己有 `shared_ptr` 成员、进不了生成表，在头里手写哈希特化）。
    ("Interface/RenderInterface/header/ShaderResourceHandle.h", "ResourceHandleKeyData", "Interface/RenderInterface"),
    # 换哈希驱动时逐个量出来的"要当键用、却还没标注"的宿主（built 目标里一共就这几个）：
    # `HashObj<InputAssemblyStates>` / `HashObj<RectSate>` / `HashObj<ViewRectData>` /
    # `HashObj<TextureSamplerDescriptor>` / `shared_dic<ShaderInfo, …>`。
    ("Interface/RenderInterface/header/CVertexInputDescriptor.h", "InputAssemblyStates", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/CPipelineStateObject.h", "RectSate", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/TextureSampler.h", "TextureSamplerDescriptor", "Interface/RenderInterface"),
    ("Interface/RenderInterface/header/ShaderProvider.h", "ShaderInfo", "Interface/RenderInterface"),
    # 曾经进不了清单的四个键类型 —— 解析它们那两个头时工具会以 exit 6 拒收（头自身或链上有错误级诊断）。
    # 那些诊断已经清掉（`openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/` 记录了每一处的红态与绿态读数），
    # 所以它们和别的键类型一样进生成表，手写 `custom_hash_trait` 特化随之删除。
    ("Interface/RenderInterface/header/GPUGraph.h", "ViewRectData", "Interface/RenderInterface"),
    ("D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h", "GPUPipelineStateKey", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h", "VertexInputBindingData", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h", "InputElementDesc", "D3D12RenderBackend",
     "graphics_backend::VertexInputBindingData::InputElementDesc"),
    # SHA-256 摘要（ShaderLibrary 的键类型）：嵌套在 cahash::sha256_hash 里。
    # ShaderCompiler 的数据模型：ShaderLibrary 的映射值类型（ShaderStructData 等）在它里面，
    # 编译环境取自 ShaderCompilerSlang 的编译单元（唯一包含它的编译单元）。
    ("Interface/ShaderCompiler/header/Compiler.h", "UniformElement", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderStructUniforms", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "BufferData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderTextureData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "SubStructReference", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderStructData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderResourceBinding", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderBindingHierarchy", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderSpaceResourceStats", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "BindingDesc", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "BufferBindingDesc", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderSpaceInfo", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderBindingInfo", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "UniformGroup", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "UniformBufferData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderResourceGroups", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "TextureData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "SamplerData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderBufferData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderBindingSpaceData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderVertexAttributeData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderReflectionData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderProgramData", "ShaderCompilerSlang"),
    ("Interface/ShaderCompiler/header/Compiler.h", "ShaderCompileTargetResult", "ShaderCompilerSlang"),
    ("CACore/header/CACore/CAHash.h", "result_type", "CACore", "cahash::sha256_hash::result_type"),
    # ShaderLibrary（Vulkan 后端）：四个无序映射，以及它们装的那一整条数据链。
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "ShaderLibrary", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanShaderCode", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanShaderFileInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "ProgramInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanShaderResourceBindingInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanDescriptorSetLayoutInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanDescriptorBinding", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanCBufferBindingInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanImageBindingInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanBufferBindingInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanSamplerBindingInfo", "VulkanRenderBackendNew"),
    ("VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h", "VulkanStructBindingInfo", "VulkanRenderBackendNew"),
    # ShaderLibrary（D3D12 后端）：同一族的另一份实现，含 ComPtr<ID3DBlob> 成员。
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderLibrary", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderSourceKey", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "CBufferBindingInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ImageBindingInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "BufferBindingInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "SamplerBindingInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "StructBindingInfos", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderResourceBindingInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderFileInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ProgramInfo", "D3D12RenderBackend"),
    ("D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h", "ShaderCode", "D3D12RenderBackend"),
    # ShaderSetData 故意不在清单里：它有一个非拥有的指针成员（`reflectionData`），指针成员在发序列化 /
    # 哈希时会被工具拒绝（退出码 15，点名该成员），而这个类型只是 `GetShaderCodes()` 的临时返回值、
    # 不参与落盘。原因与红态读数记在 change 的 design「已知边界」一节。
    # The wrapper fixture needs `Hasher.h` -> `<sha256.h>`, which only CACore's compile units carry.
    ("CACore/private/TestWrapper.h", "Holder", "CACore"),
    # The first real engine type.
    ("Interface/RenderInterface/header/GPUBuffer.h", "GPUBufferDescriptor", "Interface/RenderInterface"),
]


def codegen_generated_dir(owner_root: str) -> Path:
    """这个归属目录的产物落点：**构建目录**里的 `codegen/<模块>/`（模块名 = 归属目录把 `/` 换成 `_`）。"""
    return BUILD_DIR / "codegen" / owner_root.replace("\\", "/").strip("/").replace("/", "_")


# 合并成"一个类型一份产物"之前的三个文件名中段。它们要被清掉，而不是与新产物共存。
CODEGEN_LEGACY_SUFFIXES = (".Reflection.h", ".Serialization.h", ".Hash.h")


def cleanup_legacy_codegen_products() -> tuple[int, int, list[str]]:
    """生成段开工前清掉旧命名的产物：`out/build/*/codegen/**/<类型名>.{Reflection,Serialization,Hash}.h`。

    覆盖**所有预设**：只清当前预设的话，别的预设里留着的旧文件会在下一次构建那个预设时仍被 `#include`
    到 —— 那正是"改了新文件、编的却是旧文件"的假绿。

    为什么"先清再生成"是安全的（而 2026-10-10 那次教训说"不许先删产物"）：那次不许删的是**当时正被别的头
    include 的产物**，删掉会让解析当场找不到文件（`--emit-all exit 6 … file not found`）；这里删的是旧命名，
    源码树里已经没有它们的消费者（26 个消费点在同一次改动里全改成 `<类型名>.codegen.h`）。反过来，万一
    漏改一个 `#include`，它会因为"文件不存在"**当场失败**，而不是静默吃上一轮的旧文件。

    边界：只清旧命名的三份，**不做整目录 GC** —— 新文件名那边的孤儿产物（某个类型从清单里删掉之后留下的
    `<类型名>.codegen.h`）不在这里处理，理由与后果写在 change 的 design D5。

    返回 (删掉几份, 扫过几个预设的 codegen 目录, 删不掉的路径)。
    """
    removed = 0
    presets = 0
    leftovers: list[str] = []
    build_root = PROJECT_DIR / "out" / "build"
    if build_root.exists():
        for codegen_dir in sorted(build_root.glob("*/codegen")):
            presets += 1
            for name in CODEGEN_LEGACY_SUFFIXES:
                for stale in codegen_dir.rglob(f"*{name}"):
                    try:
                        stale.unlink()
                        removed += 1
                    except OSError:
                        leftovers.append(str(stale))
    return removed, presets, leftovers

# 每个产物都在构建期生成，并**逐份确认工具真的写出了它**（`CodeGen wrote:` 名单）。从前只有反射发射器
# 由 build.py 驱动，序列化与哈希两个发射器在构建期完全没跑过：改写它们却不重新生成入库副本，
# GeneratedSerializerTest 与 GeneratedHashTest 照样过、构建照样绿 —— 三分之二的生成链当时没有任何构建期证据。
# 这一列是产物的**文件名中段**，现在只有一项 —— 一个类型一份 `<类型名>.codegen.h`（反射 / 序列化 / 哈希
# 三节都在同一个文件里，工具那边一次 `--emit-all --out-dir` 调用写出它）。保留"名单"这个形状，是因为下面
# 几处都是按名单逐份核对存在性、逐份算哈希；从前它是三个后缀（`.Reflection.h` / `.Serialization.h` /
# `.Hash.h`），合并之后"这一行该写几份"这件事只剩一个答案。
CODEGEN_PRODUCTS = ["codegen"]

# 生成段的并发上限：每个 CodeGen.exe 实测常驻约 80 MB，8 个约 640 MB；再多就是跟编译抢内存与磁盘。
# 实际取 min(条目数, 本值, 逻辑核数 / 4)，见 codegen() 第二遍。
CODEGEN_MAX_WORKERS = 8

# 生成器把它这一次**真正读过的文件**打成这样的行（见 CodeGenMain.cpp 的 InclusionCollector）。
# build.py 把这些文件逐个算哈希进账本 —— 只哈希清单里那一个头是不够的。
CODEGEN_DEPS_PREFIX = "CodeGen deps: "
CODEGEN_WROTE_PREFIX = "CodeGen wrote: "

# 冷启动占位。清单里有不少行的头会**直接 include 别的行的生成物**（`Interface/ShaderCompiler/header/
# Compiler.h` 一个头就 include 了 24 份），而生成它自己又必须先解析那个头 —— 构建目录是空的时候这是个
# 死循环。实测（那是在"一个类型一份产物"之前测的，当时 ShaderCompilerSlang 名下有 72 份）：把它的产物
# 挪走再构建，**47 行**以退出码 6 失败（不是那一个模块的 24 行），因为失败会顺着 include 链扩散。
# 处置：登记一行要跑的时候，先给它那一份产物放一个占位文件（只在"文件不存在或还是占位内容"时放），
# 让别的行的 `#include` 解析得到；真正的产物随后由工具用改名覆盖它。失败的行的占位文件会被删掉，
# 免得空头留在包含路径上让编译阶段看到莫名其妙的东西。
CODEGEN_PLACEHOLDER = "// CodeGen 占位文件：冷启动时先让 #include 找得到，成功的产物会覆盖它。\n"

# Extra include roots the sidecar needs on top of the module env:
#   CACore/header          — the fixtures include CodeGenMeta.h, which lives in CACore/header and is NOT on
#                            the CodeGen tool's own include path.
#   Tools/CodeGen/clang_shim — a parse-time shim for the headers vendored libclang is missing. Its
#                            `cpuid.h` covers the `#include <cpuid.h>` that the Windows SDK's DirectXMath.h
#                            takes under clang's identity (`cpuid.h` is a clang RESOURCE header, and the
#                            vendored LLVM here ships no resource dir). Without it, parsing the D3D12
#                            module's own headers fails with exit 6.
CODEGEN_EXTRA_INCLUDES = ["CACore/header", "Tools/CodeGen/clang_shim"]

# Extra DEFINES for the sidecar's parse (never for the engine build):
#   _CRT_USE_BUILTIN_OFFSETOF — the UCRT `stddef.h` expands `offsetof` to the
#   null-pointer-dereference form whenever `_MSC_VER` is defined (`#if defined _MSC_VER &&
#   !defined _CRT_USE_BUILTIN_OFFSETOF`), and clang defines `_MSC_VER` on MSVC targets. MSVC folds
#   that pattern itself, clang cannot: every generated `static constexpr CodeGenFieldInfo arr[…]`
#   that uses `offsetof` then fails with "constexpr variable … must be initialized by a constant
#   expression" (measured: two errors without the define, exit 0 with it). Defining the macro makes
#   `offsetof` expand to `__builtin_offsetof`, which is what clang's own `stddef.h` uses.
#   Measured on the parse path only — the generated products keep saying `offsetof`, so MSVC's
#   compilation of them is unchanged.
CODEGEN_EXTRA_DEFINES = ["-D_CRT_USE_BUILTIN_OFFSETOF"]

COMSPEC = os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe")


def find_vs_path() -> Path:
    vswhere = (
        Path("C:/Program Files (x86)")
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if vswhere.exists():
        r = subprocess.run(
            [str(vswhere), "-latest", "-property", "installationPath"],
            capture_output=True, text=True,
        )
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip())
    for p in (
        r"C:\Program Files\Microsoft Visual Studio\18\Community",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community",
    ):
        pp = Path(p)
        if (pp / "VC" / "Auxiliary" / "Build" / "vcvars64.bat").exists():
            return pp
    raise FileNotFoundError("Visual Studio not found.")


def _write_bat(cmd: str, log_file: str | None = None) -> Path:
    vs = find_vs_path()
    v = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    ninja = (
        vs / "Common7" / "IDE" / "CommonExtensions"
        / "Microsoft" / "CMake" / "Ninja"
    )
    # Find MSVC toolchain binary directory
    msvc_base = vs / "VC" / "Tools" / "MSVC"
    msvc_ver = sorted(msvc_base.glob("*"), reverse=True)
    msvc_bin = msvc_ver[0] / "bin" / "Hostx64" / "x64" if msvc_ver else None

    lines = [
        "@echo off",
        # Sanitize PATH: MSYS2 injects entries that confuse cmd.exe parser.
        # Keep only Windows system paths before loading vcvars.
        'set "PATH=%SystemRoot%\\system32;%SystemRoot%;%SystemRoot%\\System32\\Wbem;%SystemRoot%\\System32\\WindowsPowerShell\\v1.0"',
        # Add vswhere path so vcvars can find VS installation
        'set "PATH=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer;%PATH%"',
        f'call "{v}" >nul 2>&1',
        "if %ERRORLEVEL% NEQ 0 (echo VCVARS FAILED & exit /b 1)",
    ]
    if ninja.exists():
        lines.append(f'set "PATH={ninja};%PATH%"')
    # Ensure cmake is in PATH (removed by sanitization, not added by vcvars)
    cmake_bin = Path(r"C:\Program Files\CMake\bin")
    if cmake_bin.exists():
        lines.append(f'set "PATH={cmake_bin};%PATH%"')
    # Ensure git is in PATH — CMake FetchContent/CPM runs git during configure
    # (e.g. stb populate update step); sanitization removed it.
    # Only Git\cmd is added: Git\mingw64\bin\git.exe fails to exec subcommands
    # (submodule etc.) from a plain cmd environment (exit 123), while the
    # cmd\git.exe shim resolves its real binary internally and works.
    git_cmd = Path(r"C:\Program Files\Git\cmd")
    if git_cmd.exists():
        lines.append(f'set "PATH={git_cmd};%PATH%"')
    # Ensure MSVC compiler is in PATH (vcvars may fail silently in some environments)
    if msvc_bin and msvc_bin.exists():
        lines.append(f'set "PATH={msvc_bin};%PATH%"')
        # Also set CC/CXX as fallback
        lines.append(f'set "CC={msvc_bin}\\cl.exe"')
        lines.append(f'set "CXX={msvc_bin}\\cl.exe"')
    lines.append(f'{PROJECT_DIR.drive}')
    lines.append(f'cd /d "{PROJECT_DIR}"')
    if log_file:
        lines.append(f'{cmd} > "{PROJECT_DIR / log_file}" 2>&1')
    else:
        lines.append(cmd)
    lines.append("exit /b %ERRORLEVEL%")

    bat = PROJECT_DIR / "_build_run.bat"
    bat.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")
    return bat


def run_msvc(cmd: str, log_file: str | None = None) -> None:
    print(f"\n  {cmd}")
    bat = _write_bat(cmd, log_file)

    # cmd /c: MSYS2 may convert /c paths, but we pass Windows-style paths so it's safe.
    result = subprocess.run(
        ["cmd", "/c", str(bat)],
        cwd=str(PROJECT_DIR),
    )

    if log_file:
        log_path = PROJECT_DIR / log_file
        # The log is deleted whether or not printing it worked. It used to be
        # `print(...)` followed by `unlink()`: one failed print (a console encoding error) skipped the
        # deletion, the bat APPENDED to the leftover file on every later run, and every subsequent build
        # printed its own diagnostics on top of the previous run's — which is how a stale error message
        # from an already-fixed file kept looking like the current failure.
        try:
            if log_path.exists():
                print(log_path.read_text(encoding="utf-8", errors="replace"))
            else:
                print(f"  [WARNING] Log file not found: {log_file}")
        finally:
            log_path.unlink(missing_ok=True)

    if result.returncode != 0:
        print(f"\nERROR: command failed (exit {result.returncode})")
        print(f"  Bat file kept at: {bat}")
        sys.exit(result.returncode)
    bat.unlink(missing_ok=True)


def _sha256_file(path: Path) -> str:
    """文件内容指纹，给 codegen 段"输入没变就跳过"用。文件不存在返回空串 —— 空串不等于任何真实指纹，
    所以「入库的生成物被删掉」这种情况不会被当成"没变"。"""
    if not path.is_file():
        return ""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _sha256_file_cached(path: Path, cache: dict) -> str:
    """`_sha256_file` 的带缓存版本：同一次构建里同一个文件只算一次。

    依赖名单在几十行之间大量重叠（实测 Vulkan 那一行就拉进 125 个仓库内的头），不缓存的话每一行都要
    把整套标准库重算一遍。
    """
    key = str(path)
    if key not in cache:
        cache[key] = _sha256_file(path)
    return cache[key]


def _dir_listing_hash(path: Path, cache: dict) -> str:
    """目录的**名字清单**哈希：只看这个目录下有哪些文件，不看内容。

    它防的是"新加一个同名头，把原来被读到的那个挡住" —— 依赖名单里存的是上一轮读过的路径，新文件出现
    不会改变那些路径的内容，只看名单发现不了。按名字清单判、不按修改时间判：`git checkout`、复制文件、
    切分支都会刷新时间，按时间判会把用不上这个头的行也一起重跑。
    """
    key = str(path)
    if key in cache:
        return cache[key]
    try:
        names = sorted(entry.name for entry in path.iterdir())
    except OSError:
        names = ["<unreadable>"]
    cache[key] = hashlib.sha256("\x00".join(names).encode("utf-8")).hexdigest()
    return cache[key]


def _include_dirs(env: list) -> list:
    """编译环境里的包含目录（`-I<dir>` / `/I<dir>`），相对路径按仓库根解开。"""
    dirs: list = []
    for arg in env:
        for prefix in ("-I", "/I"):
            if arg.startswith(prefix) and len(arg) > len(prefix):
                path = Path(arg[len(prefix):])
                dirs.append(path if path.is_absolute() else PROJECT_DIR / path)
                break
    return dirs


def _codegen_row_fingerprint(tool_fp: str, header_path: Path, env_fp: str, dep_paths: list,
                             include_dirs: list, outputs: dict, cache: dict) -> dict:
    """一条清单的全部输入：工具二进制、被解析的头、**生成器真正读过的每个文件**、编译环境、包含目录的
    名字清单、上一轮的产物。

    是一个**普通函数、不是循环里的闭包**：登记要跑的条目时先把参数存下来，记账发生在并发跑完之后，
    闭包那时看到的会是循环最后一次赋的值（即最后一行的路径），指纹就会张冠李戴。

    `deps` 是本条的要害：产物字节由整条包含链决定。只哈希清单里那一个文件的话，改它 include 的依赖头
    会被判"没变"而跳过 —— 编译继续吃上一轮的产物，构建却报成功（实测 63 行里有 62 行会这样被跳过）。
    """
    return {
        "tool": tool_fp,
        "header": _sha256_file_cached(header_path, cache),
        "env": env_fp,
        "deps": {p: _sha256_file_cached(Path(p), cache) for p in dep_paths},
        "dirs": {str(d): _dir_listing_hash(d, cache) for d in include_dirs},
        "out": {s: _sha256_file_cached(p, cache) for s, p in outputs.items()},
    }


def configure_inputs_changed() -> str:
    """CMake 的配置输入里，有没有比 build.ninja 更新的。返回文件名 = 该重配；返回空串 = 可以跳过。

    判据是文件的修改时间，不是"我记得上次配过"：新增或改过任何 `CMakeLists.txt` / `*.cmake` /
    `CMakePresets.json` 都会让这里返回文件名，于是照旧重配。
    """
    if not NINJA_FILE.exists():
        return "build.ninja 不存在"
    ninja_mtime = NINJA_FILE.stat().st_mtime
    candidates = [PROJECT_DIR / "CMakePresets.json"]
    for pattern in ("CMakeLists.txt", "*.cmake"):
        for p in PROJECT_DIR.rglob(pattern):
            parts = p.parts
            if "out" in parts or "_deps" in parts or ".git" in parts:
                continue
            candidates.append(p)
    for p in candidates:
        if p.is_file() and p.stat().st_mtime > ninja_mtime:
            return str(p.relative_to(PROJECT_DIR))
    return ""


def configure(offline_fetch: bool = False) -> None:
    print("=" * 60)
    print("[1/2] Configuring CMake...")
    print("=" * 60)
    # GIT_EXECUTABLE pinned to Git\cmd\git.exe: a stale cache entry pointing at
    # Git\mingw64\bin\git.exe makes FetchContent's `git submodule update`
    # fail with "submodule appears to be a git command" (that binary cannot
    # exec subcommands from a plain cmd environment).
    # Quoted: the path contains a space — without quotes cmd splits it and
    # CMake misreads the tail ("Files/Git/...") as a source directory.
    git = '"C:/Program Files/Git/cmd/git.exe"'
    # `--offline-fetch`：本机连不上 github 时用。`FETCHCONTENT_UPDATES_DISCONNECTED=ON` 跳过每个依赖的
    # update 步（就是那次 `git fetch`），`FETCHCONTENT_SOURCE_DIR_STB` 让 stb 直接用已经拉好的源码 ——
    # 只有它用 `GIT_TAG master`，每次 configure 都会联网。两句都写在这里，别再手改 CMakeCache.txt。
    offline = ""
    if offline_fetch:
        stb_src = BUILD_DIR / "_deps" / "stb-src"
        offline = (" -DFETCHCONTENT_UPDATES_DISCONNECTED=ON"
                   f" -DFETCHCONTENT_SOURCE_DIR_STB={stb_src}")
        print(f"  离线模式：不跑依赖的 update 步；stb 用本地源码 {stb_src}")
    # Check for init cache file
    init_cache = PROJECT_DIR / "_init_cache.cmake"
    if init_cache.exists():
        run_msvc(f"cmake -S {PROJECT_DIR} -B {BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE={config} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -C {init_cache} -DGIT_EXECUTABLE={git}{offline}",
                 "_build_configure.log")
    else:
        run_msvc(f"cmake --preset {CONFIGS[config]} -DGIT_EXECUTABLE={git}{offline}", "_build_configure.log")


def patch_ninja() -> None:
    if not NINJA_FILE.exists():
        return
    print("  Patching build.ninja for CMake 4.x compatibility...")
    text = NINJA_FILE.read_text(encoding="utf-8")
    cmake_long = r"C:\Program Files\CMake\bin\cmake.exe"
    inner = f'@echo off\r\nfor %A in ("{cmake_long}") do @echo %~sA'
    bat = PROJECT_DIR / "_short_path.bat"
    bat.write_text(inner + "\r\n", encoding="ascii")
    r = subprocess.run(
        ["cmd", "/c", str(bat)],
        capture_output=True, text=True,
    )
    bat.unlink(missing_ok=True)
    cmake_short = r.stdout.strip()
    if cmake_short:
        text = text.replace(cmake_long, cmake_short)

    fwd = lambda p: str(p).replace("\\", "/")
    for rel in (
        "_deps/directxtex-src/DirectXTex/Shaders/CompileShaders.cmd",
        "_deps/directxtex-src/DDSView/hlsl.cmd",
    ):
        abs_cmd = fwd(BUILD_DIR / rel)
        cmd_name = rel.rsplit("/", 1)[-1]
        text = text.replace(f" {cmd_name} >", f" {abs_cmd} >")
    NINJA_FILE.write_text(text, encoding="utf-8")


def codegen(force: bool = False) -> list[str]:
    """Build-time CodeGen step: run the libclang sidecar
    over the annotated headers, feeding each owning module's exact compile env (include dirs + defines)
    so libclang lays the types out correctly. Runs in the MSVC environment (via run_msvc so it inherits
    vcvars + sanitized PATH).

    Returns a list of human-readable failures; the caller turns a non-empty list into a non-zero build
    exit (a real tool failure and a success must NOT be indistinguishable). **An empty
    manifest is a legitimate configuration, not a failure** (empty whitelist => EXIT 0, no
    output, must not block the build). Everything abnormal - sidecar missing, sidecar non-zero exit,
    no compile env for a module, missing compile_commands.json, tool reporting it wrote fewer products
    than the row expects - is a failure.

    产物只落构建目录（`out/build/<预设>/codegen/<模块>/`），**源码树里没有生成代码**，所以这里没有"写回
    仓库"的动作，也没有"与入库副本逐字节对拍"：入库副本正是"改了类型忘了重新生成"的温床。
    """
    print()
    print("=" * 60)
    print("[1.5] CodeGen: generating reflection headers for annotated types...")
    print("=" * 60)

    # The sidecar is a CMake build target; ensure it is built first (idempotent). It is a standalone
    # tool, not an engine DLL the tester loads — building it here is safe and does NOT cause the
    # "stale product" inconsistency the build.py single-target warning cares about.
    #
    # 先看要不要问 ninja：问一次（含 vcvars 环境准备）实测几秒，占了"什么都不改"那次构建的一大半。判据是
    # 修改时间 —— 工具自己的源文件与它的 CMakeLists 都比 CodeGen.exe 旧时，ninja 只会回一句
    # "no work to do"，那就干脆不问；任何一样更新了（比如改了 CodeGenMain.cpp）都照旧问。
    # ⚠ 判据里**不包含任何 build.ninja**。曾经盯的是引擎的 `NINJA_FILE`，后来又改成工具那棵树的
    # `out/build/codegen-tool/build.ninja` —— 两个都不对：它们由 configure 重写，而"ninja 说没活干"时
    # 它们不会跟着变新，于是这条跳过**永远不再成立**（实测：连续两次构建都不打印跳过）。build.ninja 不是
    # 工具的源输入，源输入只有那些 .cpp/.h/.txt/.cmake。
    sid_path = BUILD_DIR / "bin" / "CodeGen.exe"
    tool_build_dir = PROJECT_DIR / "out" / "build" / "codegen-tool"
    tool_inputs = [p for p in (PROJECT_DIR / "Tools" / "CodeGen").rglob("*")
                   if p.is_file() and p.suffix in (".cpp", ".h", ".hpp", ".txt", ".cmake")]
    if sid_path.exists() and all(p.stat().st_mtime <= sid_path.stat().st_mtime for p in tool_inputs if p.exists()):
        print("  (工具的源文件都没动 -> 跳过 `cmake --build --target CodeGen`，省一次 vcvars + ninja)",
              flush=True)
    else:
        run_msvc(f"cmake -S {PROJECT_DIR / 'Tools' / 'CodeGen'} -B {tool_build_dir} -G Ninja "
                 f"-DCMAKE_BUILD_TYPE={config} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "
                 f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={BUILD_DIR / 'bin'}",
                 "_build_codegen_configure.log")
        run_msvc(f"cmake --build {tool_build_dir}", "_build_codegen_tool.log")

    cc_file = BUILD_DIR / "compile_commands.json"
    if not cc_file.exists():
        return [f"compile_commands.json not found at {cc_file} (configure must export it)"]
    entries = json.loads(cc_file.read_text(encoding="utf-8"))

    if not CODEGEN_ANNOTATED:
        print("  manifest is empty: nothing to generate (legitimate configuration, not a failure)")
        return []

    gen_dir = BUILD_DIR / "codegen"
    gen_dir.mkdir(parents=True, exist_ok=True)
    sid = BUILD_DIR / "bin" / "CodeGen.exe"
    try:
        env_by_root = _compile_env_by_root(entries)
    except EnvError as exc:
        # A module whose compile units disagree about the macros is refused, not guessed at.
        return [str(exc)]
    extra = [f"-I{p}" for p in CODEGEN_EXTRA_INCLUDES] + list(CODEGEN_EXTRA_DEFINES)
    failures: list[str] = []
    legacy_removed, legacy_presets, legacy_left = cleanup_legacy_codegen_products()
    print(f"  codegen: 清掉旧命名的产物 {legacy_removed} 份（扫过 {legacy_presets} 个预设的 codegen 目录）",
          flush=True)
    if legacy_left:
        failures.append("清不掉这些旧命名的产物（它们会让「所有预设里旧后缀 0 份」这条判据不成立）："
                        + ", ".join(legacy_left))

    # 「输入没变就跳过」的账本。键是清单那一行（含限定名列），值是把这一行的**全部输入**与**现有产物**
    # 一起算成的指纹：工具二进制、被解析的头、**生成器真正读过的每个文件**、编译环境、包含目录的名字
    # 清单、入库副本、上一轮的产物。少算哪一样，都会让"改了东西却继续用旧产物"变成静默的假绿 ——
    # 依赖名单那一项就是为此加的（实测：清单里 10 份头全都 include 了别的头，Vulkan 那份拉进 125 个）。
    # 全量重跑（`--codegen-all`）不读账本，行为与从前一致。
    stamp_path = BUILD_DIR / "codegen.stamp.json"
    stamp: dict = {}
    if not force and stamp_path.exists():
        try:
            stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            stamp = {}
    fresh_stamp: dict = {}
    tool_fp = _sha256_file(sid)
    # 工具二进制进了指纹：改一行 CodeGenMain.cpp 就等于"所有条目的输入都变了"，下一轮全量重生成。
    # 这是**故意保留**的安全规则 —— 试跑一条来推断"发射行为没变"曾实现过，实测被否决（见
    # docs/failed-approaches.md），因为它把"生成器改坏了"的唯一信号从"全量重跑"降成"一条样本"。
    skipped = 0
    ran = 0
    file_hashes: dict = {}   # 同一次构建里同一个文件只哈希一次（依赖名单在行与行之间高度重叠）

    # 第一遍：按清单顺序判定要跑哪些条目，并把每一行的输入固化下来。判定必须串行（账本读取与"老布局
    # 搬迁"都动文件系统），而且它很便宜；昂贵的是第二遍的解析。
    plan: list[dict] = []
    for index, row in enumerate(CODEGEN_ANNOTATED):
        # 清单的第 4 列是可选的「限定名」：工具按**最后一个非限定名**查找类型，而同一个 TU 里同名的
        # 类型很常见（实测：`result_type` 在 sha256 / komihash / STL 里都有）。带上这一列时，工具用
        # 它作为生成物里的类型名（`--qualified-name=`），避免生成的代码写上裸名字。
        rel_header, type_name, owner_root = row[0], row[1], row[2]
        qualified = row[3] if len(row) > 3 else None
        env = env_by_root.get(owner_root)
        if not env:
            failures.append(f"no compile env for module '{owner_root}' (type {type_name})")
            continue
        if not sid.exists():
            failures.append(f"codegen sidecar not found: {sid}")
            break

        env_fp = hashlib.sha256("\x00".join([*env, *extra, qualified or ""]).encode("utf-8")).hexdigest()
        header_path = PROJECT_DIR / rel_header
        # 产物按模块分目录：`out/build/<预设>/codegen/` 是所有条目共用的，而文件名只用类型名 ——
        # 清单里 `ShaderLibrary` 在 Vulkan 与 D3D12 各有一个、`ProgramInfo` 也是，两条会写同一个文件、
        # 互相覆盖（实测后果：账本里有一方的产物哈希永远对不上，每次构建都白重跑 4 条、51 秒）。
        out_dir = gen_dir / owner_root.replace("/", "_").replace("\\", "_")
        out_dir.mkdir(parents=True, exist_ok=True)
        row_key = json.dumps([rel_header, type_name, owner_root, qualified], ensure_ascii=False)
        outputs = {sfx: out_dir / f"{type_name}.{sfx}.h" for sfx in CODEGEN_PRODUCTS}
        # 老布局（产物直接躺在 codegen/ 根下、同名类型互相覆盖）里属于这一条的文件挪进新目录：
        # 换个路径不该让账本全部作废。挪进来的若不是这一条的产物（撞名的那种情况），下面照旧会重跑它。
        for sfx, new_path in outputs.items():
            legacy = gen_dir / f"{type_name}.{sfx}.h"
            if not new_path.exists() and legacy.exists():
                try:
                    legacy.replace(new_path)
                except OSError:
                    pass

        # 这一行的包含目录：编译环境里的那些 + 两个固定的额外根。目录的**名字清单**进指纹，用来发现
        # "新加一个同名头把旧头挡住"——依赖名单里存的是上一轮读过的路径，看不出新文件。
        include_dirs = _include_dirs(env) + _include_dirs(extra)

        cached = stamp.get(row_key)
        if not force and cached is not None:
            # 这一轮没跑工具，拿不到新的依赖名单，所以用**账本里记的那一份**重算：逐个文件比对内容哈希，
            # 少一个、改一个、目录里多出或少了文件，都算"输入变了"，这一行重跑。
            cached_deps = sorted((cached.get("deps") or {}))
            fp_args = (tool_fp, header_path, env_fp, cached_deps, include_dirs, outputs, file_hashes)
            if cached == _codegen_row_fingerprint(*fp_args) and all(cached["out"].values()):
                fresh_stamp[row_key] = cached
                skipped += 1
                continue
        # 要跑的条目在这里只**登记**，不启动 —— 跑是第二遍的事（并发），账本与失败汇总留在第三遍
        # （按清单顺序）。登记时先放占位文件：别的行要解析的头可能 include 这一行的产物，
        # 而第二遍是并发跑的，谁先谁后没有保证（见 CODEGEN_PLACEHOLDER 的注释）。
        for out_path in outputs.values():
            try:
                if not out_path.exists() or out_path.read_text(encoding="utf-8", errors="replace") == CODEGEN_PLACEHOLDER:
                    out_path.write_text(CODEGEN_PLACEHOLDER, encoding="utf-8")
            except OSError:
                pass   # 放不下占位不影响正确性：那一行的解析会以退出码 6 报出来
        plan.append({
            "index": index,
            "rel_header": rel_header,
            "type_name": type_name,
            "qualified": qualified,
            "env": env,
            "out_dir": out_dir,
            "outputs": outputs,
            "row_key": row_key,
            "header_path": header_path,
            "env_fp": env_fp,
            "include_dirs": include_dirs,
            "dep_paths": [],      # 由 run_row 从工具的输出里填
            "fp_cache": file_hashes,
        })

    # 第二遍：并发跑。上限 = min(条目数, CODEGEN_MAX_WORKERS, 逻辑核数 / 4)，每个进程实测常驻约 80 MB，
    # 32 逻辑核上取到 8。开工先打印用了几个，读数与日志对得上。
    ran = len(plan)
    run_failures: list[list[str]] = [[] for _ in plan]
    if plan:
        cores = os.cpu_count() or 4
        workers = max(1, min(len(plan), CODEGEN_MAX_WORKERS, max(1, cores // 4)))
        print(f"  codegen: 并发 {workers} 条（逻辑核 {cores}，上限 {CODEGEN_MAX_WORKERS}）", flush=True)

        def run_row_inner(entry: dict) -> list[str]:
            """跑一条清单：起**一次** CodeGen.exe（`--emit-all`，一次解析出一份含三节的产物），再核对它。"""
            row_failures: list[str] = []
            type_name = entry["type_name"]
            # 进度行打在**开始**时并立刻刷盘：重定向到日志时要能看出是"在推进"还是"卡住了"（实测为此
            # 误判过一次）。序号取清单序号 —— 并发下开始顺序仍是清单顺序，所以日志里是递增的。
            print(f"  [{entry['index'] + 1}/{len(CODEGEN_ANNOTATED)}] {type_name}", flush=True)
            # **不先删上一轮的产物**：产物所在的那个目录本身就在解析的包含路径上（`Compiler.h` 直接
            # `#include "UniformElement.codegen.h"`），先删就会让这一行的解析当场找不到文件
            # （实测：`--emit-all exit 6 … 'UniformElement.Serialization.h' file not found` —— 那条读数
            # 是在"一个类型一份产物"之前取的，当时那个头 include 的是旧的三个文件名）。
            # "工具退出码 0 却没写出产物"改用工具自己报的 `CodeGen wrote:` 名单来判断 —— 那比"先删掉
            # 再看有没有"更准，也不用动消费者读的那个目录。
            cmd = [str(sid), entry["rel_header"], type_name, *entry["env"], *extra,
                   "--emit-all", "--out-dir", str(entry["out_dir"])]
            if entry["qualified"]:
                cmd.append(f"--qualified-name={entry['qualified']}")
            # 诊断开关：把真正传给工具的命令行打出来（排查"清单里写了、生成物却没变"这类问题用）。
            if os.environ.get("CODEGEN_TRACE_CMD"):
                print("  cmd: " + " ".join(cmd), flush=True)
            try:
                proc = subprocess.run(cmd, cwd=str(PROJECT_DIR), capture_output=True,
                                      text=True, encoding="utf-8", errors="replace")
            except FileNotFoundError:
                return [f"codegen sidecar not found: {sid}"]
            if proc.returncode != 0:
                # 一次调用只写一份产物，所以失败也只报一条：工具非零退出时那一份还没提交
                # （它先写 `.tmp`，全部写完才改名），磁盘上不会留下半成品。
                if proc.returncode >= 0xC0000000:
                    # 这不是工具自己的退出码，是 Windows 的异常码（0xC0000135 = 找不到 DLL）。实测：
                    # 把 `out/build/<预设>/bin/libclang.dll` 挪走再强制全量生成，63 行全是这个码、
                    # stderr 是空的 —— 光印一个数字等于什么都没说，所以这里把最可能的原因点出来。
                    return [f"CodeGen 起不来（{type_name}）：退出码 {proc.returncode} "
                            f"(0x{proc.returncode:08X}) 是 Windows 的异常码，不是工具自己的退出码。"
                            f"多半是 `CodeGen.exe` 旁边缺 `libclang.dll` —— 检查 "
                            f"{sid.parent / 'libclang.dll'} 在不在，以及 CALIB_CLANG_DIR 指向的目录"]
                return [f"CodeGen '{type_name}' --emit-all exit {proc.returncode}: "
                        f"{(proc.stderr or '').strip()}"]
            # 工具报出它这一次真正读过的文件；这些文件进账本（下一轮按它们逐个比对内容哈希）。
            entry["dep_paths"] = [line[len(CODEGEN_DEPS_PREFIX):].strip()
                                  for line in (proc.stdout or "").splitlines()
                                  if line.startswith(CODEGEN_DEPS_PREFIX)]
            if not entry["dep_paths"]:
                # 拿不到名单就**不许记账**：记了就等于把"改了依赖头也不重跑"这个洞固化下来。
                return [f"CodeGen '{type_name}' --emit-all: 工具没有报出它读过的文件"
                        f"（没有 `{CODEGEN_DEPS_PREFIX}` 行）—— 拒绝为这一行记账，下次仍会重跑"]
            # 工具报出它这一次真正写出的产物（路径按 `/` 拼，这里统一成平台写法再比）。
            wrote = {str(Path(line[len(CODEGEN_WROTE_PREFIX):].strip()))
                     for line in (proc.stdout or "").splitlines()
                     if line.startswith(CODEGEN_WROTE_PREFIX)}
            absent = [str(entry["outputs"][s]) for s in CODEGEN_PRODUCTS
                      if str(entry["outputs"][s]) not in wrote]
            if absent:
                return [f"CodeGen '{type_name}' --emit-all: 工具退出码 0，却没有写出这些产物："
                        f"{', '.join(absent)} —— 多半是这个类型的标注被删了"]
            for suffix in CODEGEN_PRODUCTS:
                out_file = entry["outputs"][suffix]
                if not out_file.exists():
                    row_failures.append(f"CodeGen '{type_name}' {suffix}: 工具说它写出了 {out_file}，"
                                        f"但那个文件不在磁盘上")
                    continue
                print(f"  generated {out_file} ({out_file.stat().st_size} B)", flush=True)
            return row_failures

        def run_row(entry: dict) -> list[str]:
            """一行里出的任何岔子都要收敛成"这一行失败"，绝不能让异常穿出去。

            异常穿过 `ThreadPoolExecutor.map` 的后果实测过：**还没开始的行被整体取消**（64 行只跑了 11 行），
            构建以裸 Python traceback 结束，连 `CODEGEN FAILED` 汇总都不打印 —— 用户看到的是别人的栈，
            不是"哪一行、为什么"。所以这里兜住一切，交给第三遍按清单顺序汇总。
            """
            try:
                return run_row_inner(entry)
            except Exception as exc:  # noqa: BLE001 — 兜住的正是"意料之外"
                return [f"CodeGen '{entry['type_name']}' 这一行出错：{type(exc).__name__}: {exc}"
                        f"（工具退出码 0 却没写出产物时多半是这个类型的标注被删了）"]

        with ThreadPoolExecutor(max_workers=workers) as pool:
            # map 保序：结果按清单顺序回来，写在下面第三遍。
            run_failures = list(pool.map(run_row, plan))

    # 第三遍：按清单顺序汇总失败并记账 —— 日志与账本的顺序都不随并发调度变。
    for entry, entry_failures in zip(plan, run_failures):
        failures.extend(entry_failures)
        if entry_failures:
            # 失败的行走之前放过占位文件；工具没能覆盖它，就把它删掉 —— 空的生成头留在包含路径上，
            # 只会让后面的编译阶段报出跟真正原因无关的错。
            for out_path in entry["outputs"].values():
                try:
                    if out_path.exists() and out_path.read_text(encoding="utf-8", errors="replace") == CODEGEN_PLACEHOLDER:
                        out_path.unlink()
                except OSError:
                    pass
            continue
        # 只在这一行**没有任何失败**时记账：否则失败报过一次之后，下一轮就会因为"输入没变"被跳过，
        # 缺陷从此静默。
        fresh_stamp[entry["row_key"]] = _codegen_row_fingerprint(
            tool_fp, entry["header_path"], entry["env_fp"], entry["dep_paths"],
            entry["include_dirs"], entry["outputs"], entry["fp_cache"])

    try:
        stamp_path.write_text(json.dumps(fresh_stamp, ensure_ascii=False, sort_keys=True),
                              encoding="utf-8")
    except OSError as exc:
        print(f"  (账本写不进去：{exc} —— 下次仍会全量重跑，不影响正确性)")
    print(f"  codegen: 重跑 {ran} 条、跳过 {skipped} 条（输入没变的那些）")

    return failures


class EnvError(Exception):
    """A module's compile units disagree about their macros — refuse instead of guessing."""


def _msvc_env_parts(command: str) -> dict:
    """Split an MSVC cl.exe command into the parts a parse-only libclang run needs.

    Keeps include dirs (`/I`, `-I`), defines (`/D`, `-D`), the language standard, and — new in task
    2.3 — the FORCED INCLUDE / precompiled header (`/FI<file>`, `/Yu<file>`, `/Yc<file>`). Dropping
    `/FI`+`/Yu` silently dropped the macros a module's PCH header defines (measured: `Interface` and
    `ThreadManager` both point them at CMake's `cmake_pch.hxx`); they now become `-include <file>`.

    `/MDd` (and `/MTd`) must NOT simply be dropped. cl.exe defines `_DEBUG` and
    `_ITERATOR_DEBUG_LEVEL=2` implicitly for those runtimes, and dropping the flag dropped those macros
    too, so libclang laid the types out with the RELEASE STL rules. Measured with Tools/CodeGen/
    TestStdLayout.h: `tail` sits at byte 40 under /MD rules and at byte 48 under /MDd rules (debug STL
    gives std::string a _Container_proxy*, +8 bytes) — i.e. every offset after a std::string was wrong.
    """
    tokens = shlex.split(command, posix=False)
    # `/external:I <path>` (the separated form) → `/external:I<path>`, so the loop below has one shape
    # to handle. cl.exe accepts both spellings.
    joined: list[str] = []
    i = 0
    while i < len(tokens):
        t = tokens[i].strip('"')
        if t in ("/external:I", "-external:I") and i + 1 < len(tokens):
            joined.append(t + tokens[i + 1].strip('"'))
            i += 2
            continue
        joined.append(tokens[i])
        i += 1
    tokens = joined
    includes: list[str] = []
    defines: list[str] = []
    forced: list[str] = []
    std: str | None = None
    debug_runtime = False
    for raw in tokens:
        t = raw.strip('"')  # shlex(posix=False) keeps quote chars; strip so the arg is a bare path/def
        if t.lower() in ("/mdd", "/mtd"):
            debug_runtime = True
            continue
        # /I and /D (MSVC slash-form) get the SAME len>2 guard as -I/-D (dash-form): a bare '/I' or '/D'
        # (len 2) would otherwise emit a malformed empty '-I'/'-D' arg to libclang (AUDIT-2).
        if (t.startswith('/I') and len(t) > 2) or (t.startswith('-I') and len(t) > 2):
            includes.append('-I' + t[2:])
        # `/external:I<path>` counts as an include dir: CMake adds SYSTEM include dirs that way, and the
        # D3D12 module gets DirectX-Headers (`directx/d3dx12.h`) only through it. Dropping it made the
        # derived env unable to parse that module's own headers (measured: exit 6, `'directx/d3dx12.h'
        # file not found`). `/external:W0` / `/external:anglebrackets` are modes, not paths — the prefix
        # check below keeps them out.
        elif (t.startswith('/external:I') and len(t) > len('/external:I')) or \
             (t.startswith('-external:I') and len(t) > len('-external:I')):
            includes.append('-I' + t[len('/external:I'):])
        elif (t.startswith('/D') and len(t) > 2) or (t.startswith('-D') and len(t) > 2):
            defines.append('-D' + t[2:])
        elif t.startswith('/std:') or t.startswith('-std:'):
            std = '-std=' + t[5:]
        elif (t.startswith('/FI') and len(t) > 3) or (t.startswith('/Yu') and len(t) > 3) \
                or (t.startswith('/Yc') and len(t) > 3):
            forced.append(t[3:])
        # everything else is MSVC-only (output, warns, opt, /Fp /Fo /Fd, -c <file>) -> drop

    if debug_runtime:
        for d in ("_DEBUG", "_ITERATOR_DEBUG_LEVEL=2"):
            if not any(a.upper() == ('-D' + d).upper() for a in defines):
                defines.append('-D' + d)
    return {"includes": includes, "defines": defines, "std": std, "forced": forced}


def _msvc_to_clang_env(command: str) -> list[str]:
    """Flat clang argv for one compile command (thin wrapper over _msvc_env_parts)."""
    p = _msvc_env_parts(command)
    args = list(p["includes"]) + list(p["defines"])
    if p["std"]:
        args.append(p["std"])
    for f in p["forced"]:
        args += ["-include", f]
    return args


def _rel_to_repo(file_str: str) -> str | None:
    """Repo-relative posix path for a compile_commands entry's source file, or None if it is outside.

    Why this exists: the previous lookup was `if root in f` — a SUBSTRING test on the
    absolute path. Measured consequence: the module root `Interface` matched
    `out/build/x64-debug/_deps/assimp-src/code/CApi/CInterfaceIOWrapper.cpp` (assimp's own file), so the
    environment fed to libclang for `Interface` was assimp's (`-std=c++17`, assimp defines, no
    `-ICACore/header`). Matching is now by PATH SEGMENT against the repo root, and the build tree
    (`out/`) is excluded.
    """
    if not file_str:
        return None
    try:
        return Path(file_str.replace("\\", "/")).resolve().relative_to(PROJECT_DIR).as_posix()
    except ValueError:
        return None


def _tus_for_root(entries: list[dict], root: str) -> list[str]:
    """Compile units owned by `root` (a repo-relative DIRECTORY prefix, path-segment safe).

    The manifest's owner root is a directory such as `Tools/CodeGen`, so the match is
    `rel == root` or `rel.startswith(root + "/")` — never a substring test. Build-tree sources
    (`out/...`) are excluded: that tree holds every FetchContent dependency, and their file names
    contain module-like substrings (`CInterfaceIOWrapper.cpp` matched the root `Interface` before).
    """
    want = root.replace("\\", "/").strip("/")
    out: list[str] = []
    for e in entries:
        rel = _rel_to_repo(e.get("file", ""))
        if rel is None or rel.startswith("out/"):
            continue
        if rel == want or rel.startswith(want + "/"):
            out.append(rel)
    return out


def _env_for_root(entries: list[dict], root: str) -> list[str]:
    """Clang argv for parsing a header owned by `root`, and a one-line summary on stdout.

    Include dirs are the UNION across the root's compile units. Measured reason: the single
    "representative TU" that was used before missed include dirs other units of the same module carry
    (5 of them for `Interface` under the old, wrong root match).
    Macros must be IDENTICAL across the root's units; a second set means the answer would
    depend on which unit was picked, so the module is refused instead of guessed at.
    The forced include / precompiled header the unit used (`/FI`, `/Yu`, `/Yc`) is kept.
    """
    want = root.replace("\\", "/").strip("/")
    includes: list[str] = []
    forced: list[str] = []
    std: str | None = None
    define_sets: dict[frozenset, list[str]] = {}
    tus = 0
    for e in entries:
        rel = _rel_to_repo(e.get("file", ""))
        if rel is None or rel.startswith("out/"):
            continue
        if not (rel == want or rel.startswith(want + "/")):
            continue
        tus += 1
        parts = _msvc_env_parts(e.get("command", "") or "")
        for inc in parts["includes"]:
            if inc not in includes:
                includes.append(inc)
        for fi in parts["forced"]:
            if fi not in forced:
                forced.append(fi)
        if std is None:
            std = parts["std"]
        define_sets.setdefault(frozenset(parts["defines"]), parts["defines"])

    if not tus:
        return []
    if len(define_sets) > 1:
        detail = " | ".join("{" + ", ".join(sorted(d)) + "}" for d in define_sets)
        raise EnvError(
            f"module '{root}' has {len(define_sets)} different macro sets across its {tus} compile "
            f"unit(s): {detail} — pick one compile unit for this entry instead of letting the tool guess")
    defines = next(iter(define_sets.values()))

    args = list(includes) + list(defines)
    if std:
        args.append(std)
    for f in forced:
        args += ["-include", f]
    print(f"  env '{root}': {tus} compile unit(s), {len(includes)} include dir(s), "
          f"{len(defines)} macro(s), {len(forced)} forced include(s)"
          + (f" [{', '.join(Path(f).name for f in forced)}]" if forced else ""))
    return args


def _compile_env_by_root(entries: list[dict]) -> dict[str, list[str]]:
    """Clang argv per manifest owner root (see _env_for_root for the three tasks this implements).

    Never merges ACROSS roots: the archived change measured a `CA_STATIC_BUILD` 0/1 conflict between
    modules, which is why each root is derived on its own.
    """
    by_root: dict[str, list[str]] = {}
    for entry in CODEGEN_ANNOTATED:
        root = entry[2]
        if root in by_root:
            continue
        env = _env_for_root(entries, root)
        if env:
            by_root[root] = env
    return by_root


def build(target: str | None = None) -> None:
    print()
    print("=" * 60)
    label = target or "all targets"
    print(f"[2/2] Building {label}...")
    print("=" * 60)
    t = f" --target {target}" if target else ""
    run_msvc(f"cmake --build {BUILD_DIR}{t}", "_build_build.log")


def main() -> None:
    global config, BUILD_DIR, NINJA_FILE

    # Consume --config <name> here so its value never reaches the
    # target-argument scan below.
    config = DEFAULT_CONFIG
    args = []
    i = 0
    while i < len(sys.argv[1:]):
        a = sys.argv[1:][i]
        if a == "--config":
            if i + 1 >= len(sys.argv[1:]) or sys.argv[1:][i + 1].startswith("--"):
                print(f"ERROR: --config requires a value, one of: {list(CONFIGS)}")
                sys.exit(1)
            config = sys.argv[1:][i + 1]
            i += 2
            continue
        args.append(a)
        i += 1
    if config not in CONFIGS:
        print(f"ERROR: unknown config '{config}', expected one of: {list(CONFIGS)}")
        sys.exit(1)

    BUILD_DIR = PROJECT_DIR / "out" / "build" / CONFIGS[config]
    NINJA_FILE = BUILD_DIR / "build.ninja"

    # 两个开关随「生成物入库」一起删除，**传了就明确拒绝**。不这么写的话它们是**静默通过**的：下面逐个吃
    # 参数、只认自己认识的那几个，其余一律忽略（`git grep -n -F "unknown" -- build.py` 零命中），于是
    # 习惯了 `--no-codegen` 的人会以为「只编译不生成」，实际构建照常生成，什么提示都没有。
    removed = [a for a in args if a in ("--no-codegen", "--commit-generated")]
    if removed:
        print(f"ERROR: {removed[0]} 已随「生成物入库」一起删除 —— 生成物只落构建目录、每次构建现场生成，")
        print("       既没有「只编译、不生成」这条快路，也没有「把产物写回仓库」这个动作。")
        print("       要强制全量生成用 --codegen-all。")
        sys.exit(1)

    configure_step = "--no-configure" not in args
    force_configure = "--configure" in args
    codegen_all = "--codegen-all" in args
    offline_fetch = "--offline-fetch" in args
    target = next((a for a in args if not a.startswith("--")), None)

    print("=== CascadedEngine Build Script (Python) ===")
    print(f"  Visual Studio : {find_vs_path()}")
    print(f"  Config        : {config} (preset {CONFIGS[config]})")
    print(f"  Build dir     : {BUILD_DIR}")

    if configure_step and not force_configure:
        changed = configure_inputs_changed()
        if changed:
            print(f"  (CMake 配置输入变了：{changed} —— 重新 configure)")
        else:
            print("  (CMake 配置输入都没动 —— 跳过 configure，要强制重配就加 --configure)")
            configure_step = False

    total_start = time.perf_counter()
    configure_seconds = codegen_seconds = build_seconds = 0.0

    if configure_step:
        started = time.perf_counter()
        configure(offline_fetch=offline_fetch)
        patch_ninja()
        configure_seconds = time.perf_counter() - started
    # codegen 每次构建都跑（在没有"入库副本"之后，这是唯一的产物来源）。它需要 compile_commands.json，
    # 上一次 configure 会把它留在盘上；文件不在时 codegen() 会报出来，构建以"生成段"专属的信息失败，
    # 不会掉成一句光秃秃的 C1083。
    started = time.perf_counter()
    try:
        codegen_failures = codegen(force=codegen_all)
    except Exception as exc:  # noqa: BLE001 — 生成段的任何意外都要报成"生成失败"，不是裸 traceback
        codegen_failures = [f"生成段意外出错：{type(exc).__name__}: {exc}"]
    codegen_seconds = time.perf_counter() - started
    if codegen_failures:
        # Printed BEFORE the build: `build()` exits on the first compile error, and the report used to sit
        # after it — so when codegen and the compile both failed, the codegen list (the actionable one: a
        # tool refusal names the member and the reason) was never shown at all.
        print()
        print("=" * 60)
        print(f"CODEGEN FAILED ({len(codegen_failures)}) — the generated headers are not in the state this "
              f"build expects")
        for msg in codegen_failures:
            print(f"  - {msg}")
        print("=" * 60)
    started = time.perf_counter()
    build(target)
    build_seconds = time.perf_counter() - started
    print()
    print(f"  [耗时] configure {configure_seconds:.1f}s / codegen {codegen_seconds:.1f}s / "
          f"编译 {build_seconds:.1f}s / 合计 {time.perf_counter() - total_start:.1f}s")

    if codegen_failures:
        print()
        print("=" * 60)
        print(f"CODEGEN FAILED ({len(codegen_failures)}) — build not reported as successful")
        print("=" * 60)
        sys.exit(1)

    print()
    print("=" * 60)
    print("BUILD SUCCESSFUL")
    print("=" * 60)


if __name__ == "__main__":
    main()
