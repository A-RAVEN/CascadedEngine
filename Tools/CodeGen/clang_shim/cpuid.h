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
// 用法：build.py 把它作为额外 include 根（`CODEGEN_EXTRA_INCLUDES`）传给工具。

static inline void __cpuid(int out[4], int) noexcept
{
    out[0] = out[1] = out[2] = out[3] = 0;
}

static inline void __cpuid_count(int, int, int out[4]) noexcept
{
    out[0] = out[1] = out[2] = out[3] = 0;
}

static inline unsigned long long __get_cpuid_max(unsigned, unsigned*) noexcept
{
    return 0;
}

static inline int __get_cpuid(unsigned, unsigned out[4]) noexcept
{
    out[0] = out[1] = out[2] = out[3] = 0;
    return 0;
}
