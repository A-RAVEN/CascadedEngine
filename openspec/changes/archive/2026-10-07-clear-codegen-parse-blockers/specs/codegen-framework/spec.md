## ADDED Requirements

### Requirement: 解析期垫片不得重定义编译器内建，且必须写在文档里

`build.py` 在模块推导环境之外额外喂给工具的 include 根（`CODEGEN_EXTRA_INCLUDES`）里属于"解析期垫片"的那部分 MUST 满足三条：① MUST NOT 定义编译器自带的函数（例如 clang 的 `__cpuid`）—— 那会让工具在保真门上报 `definition of builtin function`，需要某个内建时 MUST 让编译器自己的内建接手；② 为某个名字提供定义时，参数个数 MUST 按**真实调用点**的形状给 —— 实测 Windows SDK 的 `DirectXMath.h` 在 clang 身份下 `#include <cpuid.h>`，它的 `DirectXMathMisc.inl` 在 `#if defined(__clang__) || defined(__GNUC__)` 下调用的是 6 个参数的 `__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])`；③ 每个垫片文件 MUST 在 `Tools/CodeGen/README.md` 里写明三件事：它替代哪个头、为什么仓库里需要它（实测：vendored 的 LLVM 只有 `bin/x64/libclang.dll`、没有 clang 的资源头目录）、以及它的边界（只让 include 过得去、不参与任何布局计算）。本条约束的是垫片的形状，MUST NOT 被当成"放宽解析门"的手段 —— 诊断的判法不在本条范围内。

#### Scenario: 垫片不重定义内建

- **WHEN** 用推导环境解析 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`（它的 include 链会经由
  Windows SDK 的 `DirectXMath.h` 走到 `#include <cpuid.h>`）
- **THEN** 工具的诊断列表里不出现 `definition of builtin function`；**改动之前实测为红**，逐字是
  `Tools/CodeGen/clang_shim\cpuid.h:15:20: error: definition of builtin function '__cpuid'`

#### Scenario: 垫片覆盖真实调用形状

- **WHEN** 把 `_XM_AVX2_INTRINSICS_` 打开后解析同一批头（DirectXMath 在 clang 身份下走 6 参数那一支）
- **THEN** 解析不出现 `no matching function for call to '__cpuid_count'`；**改动之前实测为红**
  （现状垫片提供的是 3 个参数的函数），只留 include 守卫时是 `use of undeclared identifier '__cpuid_count'`，
  也红

#### Scenario: README 里有据可查

- **WHEN** 在 `Tools/CodeGen/README.md` 里找垫片那一段
- **THEN** 能查到垫片文件名（`Tools/CodeGen/clang_shim/cpuid.h`）、它替代的头（`cpuid.h`）、需要它的原因
  （vendored LLVM 没有 clang 资源目录）与边界（只让 include 过得去、不参与布局计算）；
  这条判据用 `git grep -n -F "<逐字引文>" -- Tools/CodeGen/README.md` 逐条可核
