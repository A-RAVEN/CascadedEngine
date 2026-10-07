#pragma once
//
// cpuid.h — 解析期垫片（只给 CodeGen 工具用的 libclang 看，不参与引擎编译）。
//
// 为什么需要它：Windows SDK 的 `DirectXMath.h` 在 clang 身份下会 `#include <cpuid.h>`，
// 而那是 **clang 自带的资源头**（跟着 LLVM 安装在 `lib/clang/<版本>/include` 里）。本仓库 vendored
// 的那份 libclang 只带了 `bin/` + `lib/` + `include/clang-c`，没有资源目录，于是解析 D3D12 模块的
// 头文件时会以 "fatal error: 'cpuid.h' file not found" 失败（工具的退出码 6）。
//
// 这个文件里只放"让 include 过得去"的最小定义：它**不参与任何布局计算**，生成的字段表里的偏移仍然
// 由被解析的真实类型决定。引擎本体只用 MSVC 编译，不会看到这个头。
//
// ⚠ 不许在这里定义 clang 自带的函数：`__cpuid` 是 clang 的内建（MSVC 目标上就有），定义它会报
// `definition of builtin function '__cpuid'`；DirectXMath 在 `defined(__cpuid)` 为假时走的
// `__cpuid(CPUInfo, 0)` 正好就是那个内建的形状，不需要我们提供。需要 GCC 形状的名字时用宏，
// 且参数个数按**真实调用点**给 —— `DirectXMathMisc.inl` 在 clang 身份下调用的是 6 个参数的
// `__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])`。
//
// 用法：build.py 把它作为额外 include 根（`CODEGEN_EXTRA_INCLUDES`）传给工具。
// 复核：`python openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_shim_shape.py`
// （三种形状 × AVX2 开关 + 四种隔离上下文），以及同目录的 `probe_chain_clean.py`。

#define __cpuid_count(level, count, a, b, c, d)                  \
    do {                                                         \
        int r_[4] = {0};                                         \
        __cpuidex(r_, (level), (count));                         \
        (a) = r_[0]; (b) = r_[1]; (c) = r_[2]; (d) = r_[3];      \
    } while (0)
