// 形状样板：哪些成员形状工具必须拒绝、哪些必须能出码。每一个类型都在 README 第五节那张表里有
// 一行，对应一条实测退出码。
//
// 指针成员的边界来自老运行时与新运行时的差别：老运行时在序列化里"看见指针就跳过"（一个字节都不写，
// 数据静默丢失），新运行时没有这条跳过，于是生成的代码会真的去写这个成员。两者都不能接受，
// 所以含指针成员的类型在发序列化 / 哈希时拒绝，并点名是哪个成员。
#pragma once

#include "CodeGenMeta.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace codegen_shapes
{
    CA_REFLECT(serializable)
    struct Inner
    {
        int x;
        int y;
    };

    // ---- 支持：全部公有成员、标量 / 字符串 / 结构体 / 三种容器 / 定长数组 ----
    CA_REFLECT(serializable)
    struct Supported
    {
        int                          scalar;
        std::string                  text;
        Inner                        nested;
        std::vector<int>             values;
        std::map<int, int>           ordered;
        std::unordered_map<int, int> hashed;
        int                          fixed[4];
    };

    // ---- 拒绝：裸指针成员 ----
    CA_REFLECT(serializable)
    struct RawPointerMember
    {
        int* p;
        int  tag;
    };

    // ---- 拒绝：托管指针成员（shared_ptr / unique_ptr 这一类）----
    CA_REFLECT(serializable)
    struct ManagedPointerMember
    {
        std::shared_ptr<int> p;
        int                  tag;
    };

    // ---- 拒绝：容器的元素是指针 ----
    CA_REFLECT(serializable)
    struct PointerElement
    {
        std::vector<int*> items;
    };

    // ---- 拒绝：重复键容器（等键之间没有规范顺序）----
    CA_REFLECT(serializable)
    struct MultiKey
    {
        std::multimap<int, int> m;
    };

    // ---- 拒绝：按位存储的容器 ----
    CA_REFLECT(serializable)
    struct BitPacked
    {
        std::vector<bool> v;
    };

    // ---- 拒绝：元素不可默认构造 ----
    struct NoDefaultCtor
    {
        NoDefaultCtor() = delete;
        explicit NoDefaultCtor(int v) : v(v) {}
        int v;
    };

    CA_REFLECT(serializable)
    struct NoDefaultElement
    {
        std::vector<NoDefaultCtor> v;
    };

    // ---- 拒绝：无序容器的键没有全序 ----
    struct NoOrderKey
    {
        int v;
    };

    CA_REFLECT(serializable)
    struct NoOrderKeyMap
    {
        std::unordered_map<NoOrderKey, int> m;
    };
} // namespace codegen_shapes
