// `TResource` 的读写换成生成代码之后，磁盘格式必须与老路相同。
//
// 四个读数（同一个对象）：
//   ① `TResource::Serialize` 写进块里的字节，与**老路**（`cacore::batch_serializer` 逐成员写）写出的
//      字节**逐字节相同** —— 老路已经随三份旧头退场，所以"老的一侧"在这里是**写死的期望值**
//      （2026-10-05 用老序列化器实测出来的 16 字节；8.1 的 301 字节期望值也是这个做法）；
//   ② `TResource::Serialize` 只是把生成代码写出的那一段**原样转发**给批（不自己加长度前缀之类的料）——
//      所以批里收到的字节与 `CodeGenSerializer` 直接写进 `castl::vector` 的字节相同；
//   ③ 把批里收到的字节喂回新读侧（走 `TResource::Deserialize`），字段逐个读回来一致；
//   ④ 反向对照：改一个字段，写出的字节必须跟着变（否则①那条"相同"是空的）。
//
// 批用探针接管（`WriteBatchStub` / `ReadBatchStub`），不碰文件系统。
#include "TestResource.h"

#include "NewRes.codegen.h"

#include <CodeGenSerialization.h>
#include <cstdio>
#include <cstring>

namespace
{
    // 老路写出来的 16 字节：`int 42` / `float 1.5` / `double 2.25`，逐成员写、没有长度前缀。
    const char* const kOldBytesHex = "2a0000000000c03f0000000000000240";

    castl::vector<unsigned char> hexToBytes(const char* hex)
    {
        castl::vector<unsigned char> out;
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
                out.push_back(static_cast<unsigned char>((high << 4) | value));
                high = -1;
            }
        }
        return out;
    }

    bool sameBytes(const castl::vector<unsigned char>& a, const castl::vector<unsigned char>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    // 把 `Write` 收进一块 castl::vector 的批探针：`TResource::Serialize` 交给批的那一段直接落进来，
    // 于是"它写出去的字节"与"生成代码写出的字节"可以直接比。
    class WriteBatchStub : public ca_io::WBatch
    {
    public:
        castl::vector<unsigned char> bytes;

        void Write(uint64_t size, void const* data) override
        {
            const auto* p = static_cast<const unsigned char*>(data);
            if (p != nullptr)
                bytes.insert(bytes.end(), p, p + size);
            ++m_submits;
        }
        void     Seek(uint64_t) override {}
        void     SubmitAndWait() override {}
        uint32_t SubmitCount() const override { return m_submits; }

    private:
        uint32_t m_submits = 0;
    };

    // 从同一块字节里流式读回来的批探针。读越界时**不动**位置，让上层的读对象自己发现失败。
    class ReadBatchStub : public ca_io::IOBatch
    {
    public:
        explicit ReadBatchStub(const castl::vector<unsigned char>& src) : m_bytes(src) {}

        void Read(uint64_t size, void* destination) override
        {
            if (m_position + size > m_bytes.size())
                return;
            std::memcpy(destination, m_bytes.data() + m_position, static_cast<std::size_t>(size));
            m_position += static_cast<std::size_t>(size);
        }
        void     Seek(uint64_t offset) override { m_position = static_cast<std::size_t>(offset); }
        void     SubmitAndWait() override {}
        uint32_t SubmitCount() const override { return 1; }

    private:
        castl::vector<unsigned char> m_bytes;
        std::size_t                  m_position = 0;
    };
} // namespace

bool RunResourceBatchTest()
{
    constexpr int    A = 42;
    constexpr float  B = 1.5f;
    constexpr double C = 2.25;

    codegen_resource::NewRes object;
    object.a = A;
    object.b = B;
    object.c = C;

    // ---- 新路：`TResource::Serialize` 把生成代码写出的那一段整段交给块。
    WriteBatchStub newStub;
    object.Serialize(&newStub);

    // ---- ① 与老路的字节逐字节相同。
    const castl::vector<unsigned char> oldBytes = hexToBytes(kOldBytesHex);
    const bool batchBytesMatchOld = sameBytes(newStub.bytes, oldBytes);

    // ---- ② 转发检查：块里收到的，必须与生成代码直接写的那一段逐字节相同。
    castl::vector<unsigned char> stage;
    cacore::codegen::CodeGenSerializer<codegen_resource::NewRes>::serialize(object, stage);
    const bool onlyForwarded = sameBytes(newStub.bytes, stage);

    // ---- ③ 读回来：走 `TResource::Deserialize`（新的流式读）。
    codegen_resource::NewRes readBack;
    ReadBatchStub reader(newStub.bytes);
    readBack.Deserialize(&reader);
    const bool readBackOk = readBack.a == A && readBack.b == B && readBack.c == C;

    // ---- ④ 反向对照：改一个字段，写出的字节必须跟着变。
    codegen_resource::NewRes control = object;
    control.a = A + 1;
    castl::vector<unsigned char> controlStage;
    cacore::codegen::CodeGenSerializer<codegen_resource::NewRes>::serialize(control, controlStage);
    const bool controlDiffers = !sameBytes(controlStage, stage);

    std::printf("TResource 对拍：新批字节 == 老批字节 = %d（新=%zu / 老=%zu），只做转发 = %d，读回一致 = %d，反向对照不同 = %d\n",
                batchBytesMatchOld ? 1 : 0, newStub.bytes.size(), oldBytes.size(), onlyForwarded ? 1 : 0,
                readBackOk ? 1 : 0, controlDiffers ? 1 : 0);

    const bool ok = batchBytesMatchOld && onlyForwarded && readBackOk && controlDiffers;
    if (ok)
        std::puts("CodeGen TResource batch roundtrip PASSED");
    return ok;
}
