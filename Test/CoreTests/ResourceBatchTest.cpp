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

        // 返回本次写是否被接受：这块内存探针永远接受（写进 `bytes`），语义与实现的"排队即接受"一致。
        bool Write(uint64_t size, void const* data) override
        {
            const auto* p = static_cast<const unsigned char*>(data);
            if (p != nullptr)
                bytes.insert(bytes.end(), p, p + size);
            ++m_submits;
            return true;
        }
        void     Seek(uint64_t) override {}
        void     SubmitAndWait() override {}
        uint32_t SubmitCount() const override { return m_submits; }
        // 来源路径：`TResource::Serialize` 侧的失败消息靠它点名"写到哪儿"，这里是一个能认出来的名字。
        castl::string const& SourcePath() const override { return m_sourcePath; }

    private:
        uint32_t m_submits = 0;
        castl::string m_sourcePath = "WriteBatchStub://内存字节去向";
    };

    // 从同一块字节里流式读回来的批探针。读越界时**不动**位置并返回 false
    // —— 让上层的读对象自己发现失败（design D3）。
    class ReadBatchStub : public ca_io::IOBatch
    {
    public:
        explicit ReadBatchStub(const castl::vector<unsigned char>& src) : m_bytes(src) {}

        bool Read(uint64_t size, void* destination) override
        {
            if (m_position + size > m_bytes.size())
                return false;
            std::memcpy(destination, m_bytes.data() + m_position, static_cast<std::size_t>(size));
            m_position += static_cast<std::size_t>(size);
            return true;
        }
        void     Seek(uint64_t offset) override { m_position = static_cast<std::size_t>(offset); }
        void     SubmitAndWait() override {}
        uint32_t SubmitCount() const override { return 1; }
        // 来源路径：`TResource::Deserialize` 失败时抛出的 `io_error` 要能点名它。
        castl::string const& SourcePath() const override { return m_sourcePath; }

    private:
        castl::vector<unsigned char> m_bytes;
        std::size_t                  m_position = 0;
        castl::string m_sourcePath = "ReadBatchStub://内存字节来源";
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

    // ---- ⑤（任务 2.1）流式来源拒绝读时，读对象必须把失败传播出来 —— 与缓冲区分支同一失败语义。
    // 载荷截掉末 4 字节（16 → 12）：读第三个字段 `c`（double，8 字节，位置 8）时来源拒绝（8 + 8 > 12），
    // 整条 `deserialize` 必须返回 false。改前 `read_raw` 流式分支无条件 `return true`，这里拿到 true。
    castl::vector<unsigned char> truncatedStage(stage.begin(), stage.begin() + 12);
    ReadBatchStub truncatedReader(truncatedStage);
    codegen_resource::NewRes truncatedObject;
    std::size_t truncatedPosition = 0;
    const bool streamTruncatedFails =
        !cacore::codegen::CodeGenSerializer<codegen_resource::NewRes>::deserialize(truncatedObject, truncatedReader,
                                                                                   truncatedPosition);

    // ---- ⑥（任务 3.1）截短载荷经 `TResource::Deserialize` 必须抛 `ca_io::io_error`，且消息点名来源路径。
    // 改前 `TResource::Deserialize` 忽略 `deserialize` 的返回值：不抛、字段是垃圾。
    bool resourceTruncatedThrows = false;
    bool resourceTruncatedNamesPath = false;
    {
        codegen_resource::NewRes truncatedReadBack;
        ReadBatchStub shortReader(truncatedStage);
        try
        {
            truncatedReadBack.Deserialize(&shortReader);
        }
        catch (ca_io::io_error const& e)
        {
            resourceTruncatedThrows = true;
            // 直接对 `what()` 的字符缓冲做子串查找，不额外复制 std::string（本进程里那类堆缓冲
            // 在异常路径上不可靠，见 `g_errorDetail` 的注释；CoreTests 没有那个已知问题，但没必要冒）。
            resourceTruncatedNamesPath = std::strstr(e.what(), shortReader.SourcePath().c_str()) != nullptr;
        }
        catch (...)
        {
        }
    }

    std::printf("TResource 对拍：新批字节 == 老批字节 = %d（新=%zu / 老=%zu），只做转发 = %d，读回一致 = %d，反向对照不同 = %d\n",
                batchBytesMatchOld ? 1 : 0, newStub.bytes.size(), oldBytes.size(), onlyForwarded ? 1 : 0,
                readBackOk ? 1 : 0, controlDiffers ? 1 : 0);
    std::printf("流式读对象截短载荷（16→12 字节）：来源拒绝读后 deserialize 返回失败 = %d（期望 1，改前为 0）\n",
                streamTruncatedFails ? 1 : 0);
    std::printf("资源层截短载荷：TResource::Deserialize 抛 ca_io::io_error = %d、消息含来源路径 = %d（均期望 1，改前均为 0）\n",
                resourceTruncatedThrows ? 1 : 0, resourceTruncatedNamesPath ? 1 : 0);

    const bool ok = batchBytesMatchOld && onlyForwarded && readBackOk && controlDiffers && streamTruncatedFails &&
                    resourceTruncatedThrows && resourceTruncatedNamesPath;
    if (!ok)
    {
        std::printf("TResource 批读写判据失败：字节对拍 = %d、只做转发 = %d、读回一致 = %d、反向对照不同 = %d、"
                    "流式截短失败 = %d、资源层截短抛 io_error = %d、消息含来源路径 = %d\n",
                    batchBytesMatchOld ? 1 : 0, onlyForwarded ? 1 : 0, readBackOk ? 1 : 0, controlDiffers ? 1 : 0,
                    streamTruncatedFails ? 1 : 0, resourceTruncatedThrows ? 1 : 0,
                    resourceTruncatedNamesPath ? 1 : 0);
        return false;
    }
    std::puts("CodeGen TResource batch roundtrip PASSED");
    return true;
}
