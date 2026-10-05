//
// CodeGenRuntimeTest.cpp — CodeGen 自有运行时（读写对象）的判据。
//
// 这个文件回答三个问题，每个都给两次读数（新的一侧 / 旧的一侧）：
//
//   1. 新读对象能不能读回**旧运行时写出的**载荷，逐字段一致；
//   2. 新写对象与旧运行时写同一个对象，字节是不是逐字节相同；
//   3. 载荷被截断时，读对象在边界失败，而不是读越界内存。
//
// 「旧的一侧」现在是当场算出来的（旧运行时还在）。本 change 收尾时旧头会被删除，那时这一半改成
// 冻结成字面量的期望字节，判据本身不变：新读写写出的字节必须等于旧协议写出的那串字节。
//
#include <CodeGenSerialization.h>     // 新运行时：本文件真正要验的东西
#include <CASTL/CAArray.h>
#include <CASTL/CAVector.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "TestEmit.h"                 // codegen_demo::EmitSample（int / float / bool / double）

namespace
{
    using codegen_demo::EmitSample;

    // ---- 正面判据（每条都能失败：把新运行时的形状改回去，断言就红）。
    //
    // 1.1：读位置必须是**调用方持有的变量**（引用）。旧读对象把位置私有化、也没有取值方法，
    //      生成代码因此不可能在字节流的中间把位置交给另一个类型的生成代码 —— 这是「容器里装结构体」
    //      只能被拒绝（旧工具退出码 12）的唯一原因。实测的改之前读数是一条编译错误：
    //      `error C2248`：旧读对象的偏移量成员是私有的，测试里那个直接取它的探针编不过
    //      （2026-10-03 的实测读数；那个旧读对象已随三份旧头删除）。
    static_assert(std::is_lvalue_reference_v<decltype(
                      std::declval<cacore::codegen::reader<castl::vector<uint8_t>>&>().position())>,
                  "读位置必须是调用方持有的变量（左值引用）；返回副本就等于把位置藏回对象里，这条不成立");

    // 1.4：读入口必须能报告失败。旧读对象的读取返回 void，读到长度之外没有任何可观察的失败。
    static_assert(std::is_same_v<decltype(std::declval<cacore::codegen::reader<castl::vector<uint8_t>>&>()
                                              .read(std::declval<int&>())),
                                 bool>,
                  "读入口必须返回 bool 报告失败；返回 void 就是旧读对象那种『越界也读』，这条不成立");

    bool sameBytes(const castl::vector<uint8_t>& a, const castl::vector<uint8_t>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    // 旧运行时写出来的字节（旧的一侧）—— 2026-10-05 用旧序列化器实测后写死在这里：三份旧头随本
    // change 删掉之后，"旧的一侧"只能以期望值的形式留下（8.1 的 301 字节期望值就是这个做法）。
    //   ① `EmitSample{10, 3.5f, true, 7.25}`：int / float / bool / double，共 17 字节
    //   ② `std::vector<unsigned>{1,2,3}`：个数 + 三个元素，共 20 字节
    //   ③ `std::string "abcdefg"`：长度 + 内容（**没有**结尾的 \0），共 15 字节
    const char* const kOldSampleHex = "0a00000000006040010000000000001d40";
    const char* const kOldVectorHex = "0300000000000000010000000200000003000000";
    const char* const kOldStringHex = "070000000000000061626364656667";

    castl::vector<uint8_t> hexToBytes(const char* hex)
    {
        castl::vector<uint8_t> out;
        int high = -1;
        for (const char* p = hex; *p != '\0'; ++p)
        {
            const char c = *p;
            int value = -1;
            if (c >= '0' && c <= '9')
                value = c - '0';
            else if (c >= 'a' && c <= 'f')
                value = c - 'a' + 10;
            else
                continue;
            if (high < 0)
                high = value;
            else
            {
                out.push_back(static_cast<uint8_t>((high << 4) | value));
                high = -1;
            }
        }
        return out;
    }
} // namespace

bool RunCodeGenRuntimeTest()
{
    const EmitSample in{ 10, 3.5f, true, 7.25 };

    // ---- 写对象只往后追加，字节与旧运行时逐字节相同。
    castl::vector<uint8_t> newBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(newBytes);
        w.write(in.a);
        w.write(in.b);
        w.write(in.c);
        w.write(in.d);
    }
    const castl::vector<uint8_t> oldBytes = hexToBytes(kOldSampleHex);
    const bool writeMatchesOld = sameBytes(newBytes, oldBytes);

    // ---- 读对象读回旧运行时写出的载荷，逐字段一致。
    EmitSample out{};
    std::size_t position = 0;
    bool readOk = false;
    {
        cacore::codegen::reader<castl::vector<uint8_t>> r(oldBytes, position);
        readOk = r.read(out.a) && r.read(out.b) && r.read(out.c) && r.read(out.d) && r.ok();
    }
    const bool fieldsMatch = readOk && out.a == in.a && out.b == in.b && out.c == in.c && out.d == in.d &&
                             position == oldBytes.size();

    // ---- 容器那一层：容器载荷同样读得回来。
    const std::vector<unsigned> vecIn{ 1u, 2u, 3u };
    const castl::vector<uint8_t> vecOldBytes = hexToBytes(kOldVectorHex);
    castl::vector<uint8_t> vecNewBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(vecNewBytes);
        cacore::codegen::cgWriteContainer(w, vecIn);
    }
    std::vector<unsigned> vecOut;
    bool vecReadOk = false;
    {
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(vecOldBytes, p);
        vecReadOk = cacore::codegen::cgReadContainer(r, vecOut) && r.ok();
    }
    const bool containerMatch = sameBytes(vecNewBytes, vecOldBytes) && vecReadOk && vecOut == vecIn;

    // ---- 字符串那一层：字符串写成「先个数、再字节」，没有结尾的 \0。
    const std::string text = "abcdefg";
    const castl::vector<uint8_t> textOldBytes = hexToBytes(kOldStringHex);
    castl::vector<uint8_t> textNewBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(textNewBytes);
        cacore::codegen::cgWriteValue(w, text);
    }
    std::string textOut;
    bool textReadOk = false;
    {
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(textOldBytes, p);
        textReadOk = cacore::codegen::cgReadValue(r, textOut) && r.ok();
    }
    const bool stringContract = sameBytes(textNewBytes, textOldBytes) && textReadOk && textOut == text &&
                                textOldBytes.size() == sizeof(unsigned long long) + text.size();


    // ---- 截断的载荷在边界失败，不读越界，且失败可被调用方观察到。
    const castl::vector<uint8_t> truncated(oldBytes.begin(), oldBytes.begin() + 9); // 只够 a / b / c
    EmitSample partial{};
    std::size_t truncatedPosition = 0;
    bool truncatedFailed = false;
    {
        cacore::codegen::reader<castl::vector<uint8_t>> r(truncated, truncatedPosition);
        const bool a = r.read(partial.a);
        const bool b = r.read(partial.b);
        const bool c = r.read(partial.c);
        const bool d = r.read(partial.d);   // 这一步必须失败：d 在长度之外
        truncatedFailed = a && b && c && !d && !r.ok() && truncatedPosition == truncated.size();
    }

    // 对照：完整的载荷读起来必须成功（否则上面那条「失败」可能只是因为读路径根本不工作）。
    bool fullPayloadSucceeds = false;
    {
        EmitSample full{};
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(oldBytes, p);
        fullPayloadSucceeds = r.read(full.a) && r.read(full.b) && r.read(full.c) && r.read(full.d) && r.ok();
    }

    // ---- 定长数组：C 数组与 `castl::array`（== `std::array`）都按位置逐个覆写。
    // `castl::array` 没有 `clear()`，所以这里同时也是"读取路径不许假定容器一定有 clear()"的判据：
    // 一旦读路径退回去调用 `c.clear()`，这一行就编不过。
    const int source[3] = { 11, 22, 33 };
    castl::vector<uint8_t> arrayBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(arrayBytes);
        cacore::codegen::cgWriteContainer(w, source);
    }
    int cArrayOut[3] = { 0, 0, 0 };
    bool cArrayOk = false;
    {
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(arrayBytes, p);
        cArrayOk = cacore::codegen::cgReadContainer(r, cArrayOut) && r.ok();
    }
    const bool cArrayValues = cArrayOut[0] == 11 && cArrayOut[1] == 22 && cArrayOut[2] == 33;

    const castl::array<int, 3> fixedIn = { 4, 5, 6 };
    castl::vector<uint8_t> fixedBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(fixedBytes);
        cacore::codegen::cgWriteContainer(w, fixedIn);
    }
    castl::array<int, 3> fixedOut = { 9, 9, 9 };
    bool fixedOk = false;
    {
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(fixedBytes, p);
        fixedOk = cacore::codegen::cgReadContainer(r, fixedOut) && r.ok();
    }
    const bool fixedValues = fixedOut[0] == 4 && fixedOut[1] == 5 && fixedOut[2] == 6;

    // 长度与 extent 不符：载荷说 4 个元素，目标是 3 个 —— 必须失败，并报出两个长度。
    castl::vector<uint8_t> badLengthBytes;
    {
        cacore::codegen::writer<castl::vector<uint8_t>> w(badLengthBytes);
        w.write(static_cast<unsigned long long>(4));
        for (int i = 0; i < 4; ++i)
            w.write(i);
    }
    int badTarget[3] = { 7, 7, 7 };
    std::size_t reportedExtent = 0;
    std::size_t reportedPayload = 0;
    bool mismatchDetected = false;
    {
        std::size_t p = 0;
        cacore::codegen::reader<castl::vector<uint8_t>> r(badLengthBytes, p);
        mismatchDetected = !cacore::codegen::cgReadContainer(r, badTarget) && !r.ok() &&
                           r.lengthMismatch(reportedExtent, reportedPayload);
    }
    const bool mismatchReported = mismatchDetected && reportedExtent == 3 && reportedPayload == 4;

    // ---- 10.2 的第五条规则：定长数组。新旧两套写**同一个**数组，字节必须逐字节相同。
    // 这一条只有 `castl::array` 能对拍：裸 C 数组走不了旧路径（旧序列化器按自己的容器判据分派，
    // 它只认有容器特征的类型，裸数组不在其中），所以 C 数组那一侧由上面的逐元素覆写与长度校验覆盖。
    // `castl::array<int,3>{4,5,6}` 的旧字节 = 长度 3 + 三个 int（与上面 `vector` 同一口径）。
    const castl::vector<uint8_t> fixedOldBytes = hexToBytes("0300000000000000040000000500000006000000");
    const bool fixedMatchesOld = sameBytes(fixedBytes, fixedOldBytes);

    std::printf("CodeGen runtime write == old runtime bytes = %d (bytes=%zu)\n",
                writeMatchesOld ? 1 : 0, newBytes.size());
    std::printf("CodeGen runtime reads old payload field-by-field = %d (consumed=%zu/%zu)\n",
                fieldsMatch ? 1 : 0, position, oldBytes.size());
    std::printf("CodeGen runtime container bytes == old, and reads back = %d\n", containerMatch ? 1 : 0);
    std::printf("CodeGen runtime string bytes == old = %d (bytes=%zu, 载荷长度 = 8 + 文本长度 = %zu)\n",
                stringContract ? 1 : 0, textOldBytes.size(), sizeof(unsigned long long) + text.size());
    std::printf("CodeGen runtime truncated payload fails at the boundary = %d, full payload succeeds = %d\n",
                truncatedFailed ? 1 : 0, fullPayloadSucceeds ? 1 : 0);
    std::printf("CodeGen runtime fixed arrays (C array = %d, castl::array = %d) overwritten in place; "
                "length mismatch fails and reports extent=%zu vs payload=%zu; castl::array bytes == old = %d\n",
                (cArrayOk && cArrayValues) ? 1 : 0, (fixedOk && fixedValues) ? 1 : 0,
                reportedExtent, reportedPayload, fixedMatchesOld ? 1 : 0);

    const bool ok = writeMatchesOld && fieldsMatch && containerMatch && stringContract && truncatedFailed &&
                    fullPayloadSucceeds && cArrayOk && cArrayValues && fixedOk && fixedValues &&
                    mismatchReported && fixedMatchesOld;
    if (ok)
        std::puts("CodeGen runtime read/write PASSED");
    return ok;
}
