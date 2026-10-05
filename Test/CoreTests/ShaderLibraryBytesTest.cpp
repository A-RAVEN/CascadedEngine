// D3D12 后端 `graphics_backend::ShaderLibrary` 的迁移判据：生成代码写出的字节与老路对得上。
//
// 这个类型在 D3D12RenderBackend 的私有目录里，CoreTests 借它的私有头与生成物目录把它编进来 ——
// 只用头里的定义与生成的序列化入口，不调用那个模块库里的函数。
//
// 老的一侧（老描述符 + 旧的批序列化器）只活在量那一次的探针里：老描述符今天
// 已经从这个类型上删掉了，下面两串十六进制就是那一次量到的老字节。
//
// 四个读数：
//   ① 不含程序的对象：新写出的字节与老字节**逐字节相同**（504 B）；
//   ② 含一个程序（4 字节 blob）的对象：老的一侧把 `ComPtr<ID3DBlob>` 整个跳过（旧运行时先判托管指针，
//      指针成员一个字节都不写），新的一侧按内容写 —— 两串字节只差**插入的那 12 字节**（`uint64 4` + `"ABCD"`），
//      插入点之前与之后都逐字节对齐，插入段就是 blob 本身；
//   ③ 往返：新写出的字节读回来，结构体那一半字段一致，blob 那一半字节数与内容都不丢；再写一遍字节不变；
//   ④ 反向对照：blob 内容换成同样长度的 `"WXYZ"`，长度不变、只有那 4 个字节变。
#include <ShaderLibrary/ShaderLibrary.h>

#include <cstdio>
#include <cstring>

namespace
{
    const char* const kOldNoProgramHex = R"HEX(
0000000000000000000000000000000001000000000000000a00000000000000546573745374727563740a0000000000
000054657374537472756374400000001000000001000000000000000800000000000000666c6f617434783407000000
000000006d5f4d6f64656c0000000040000000400000000100000001000000000000000800000000000000675f416c62
65646f010100000001000000000000000900000000000000675f53616d706c6572010000000000000007000000000000
00675f426f6e657301080000000100000000000000030000000000000073756209000000000000005375625374727563
740200000010000000300000003000000001000000000000001000000000000000526f6f745374727563742e736c616e
670a0000000000000054657374537472756374400000001000000001000000000000000800000000000000666c6f6174
34783407000000000000006d5f4d6f64656c000000004000000040000000010000000100000000000000080000000000
0000675f416c6265646f010100000001000000000000000900000000000000675f53616d706c65720100000000000000
0700000000000000675f426f6e6573010800000001000000000000000300000000000000737562090000000000000053
756253747275637402000000100000003000000030000000
)HEX";

    const char* const kOldWithProgramHex = R"HEX(
000000000000000001000000000000002000000000000000070000000000000000000000000000000000000000000000
00000000000000000401000000000000000a00000000000000546573742e736c616e6704000000000000006d61696e01
000000000000000a00000000000000546573745374727563740a00000000000000546573745374727563744000000010
00000001000000000000000800000000000000666c6f617434783407000000000000006d5f4d6f64656c000000004000
0000400000000100000001000000000000000800000000000000675f416c6265646f0101000000010000000000000009
00000000000000675f53616d706c657201000000000000000700000000000000675f426f6e6573010800000001000000
000000000300000000000000737562090000000000000053756253747275637402000000100000003000000030000000
01000000000000001000000000000000526f6f745374727563742e736c616e670a000000000000005465737453747275
6374400000001000000001000000000000000800000000000000666c6f617434783407000000000000006d5f4d6f6465
6c0000000040000000400000000100000001000000000000000800000000000000675f416c6265646f01010000000100
0000000000000900000000000000675f53616d706c657201000000000000000700000000000000675f426f6e65730108
000000010000000000000003000000000000007375620900000000000000537562537472756374020000001000000030
00000030000000
)HEX";

    // 十六进制文本 → 字节；换行与空格不是十六进制字符，直接跳过。
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

    // 第一处不同的位置；全同则返回 a.size()。
    std::size_t firstDifference(const castl::vector<unsigned char>& a, const castl::vector<unsigned char>& b)
    {
        const std::size_t n = a.size() < b.size() ? a.size() : b.size();
        std::size_t i = 0;
        while (i < n && a[i] == b[i])
            ++i;
        return i;
    }

    // 一份内容固定（全部值初始化后逐字段赋值）的结构体数据：两个映射共用它。
    ShaderCompilerSlang::ShaderStructData makeStructData()
    {
        ShaderCompilerSlang::ShaderStructData sd{};
        sd.m_TypeName = cacore::NameHash("TestStruct");
        sd.m_StructUniforms.SetSize(64u, 16u);
        ShaderCompilerSlang::UniformElement element{};
        element.Init(cacore::NameHash("float4x4"), cacore::NameHash("m_Model"), 0u, 64u, 64u, 1u);
        sd.m_StructUniforms.m_Elements.push_back(element);
        ShaderCompilerSlang::ShaderTextureData texture{};
        texture.m_Name = cacore::NameHash("g_Albedo");
        texture.m_RWType = ShaderCompilerSlang::EShaderResourceAccess::eReadOnly;
        texture.m_ElementCount = 1u;
        sd.m_Textures.push_back(texture);
        sd.m_TextureSamplers.push_back(cacore::NameHash("g_Sampler"));
        ShaderCompilerSlang::BufferData buffer{};
        buffer.m_Name = cacore::NameHash("g_Bones");
        buffer.m_RWType = ShaderCompilerSlang::EShaderResourceAccess::eReadOnly;
        buffer.m_ElementCount = 8u;
        sd.m_Buffers.push_back(buffer);
        ShaderCompilerSlang::SubStructReference sub{};
        sub.m_Name = cacore::NameHash("sub");
        sub.m_StructTypeName = cacore::NameHash("SubStruct");
        sub.m_ElementCount = 2u;
        sub.m_MemoryOffset = 16u;
        sub.m_ElementMemorySize = 48u;
        sub.m_Stride = 48u;
        sd.m_SubStructReferences.push_back(sub);
        return sd;
    }

    constexpr unsigned char kProgramHashFirstByte = 7;

    cahash::sha256_hash::result_type programHash()
    {
        cahash::sha256_hash::result_type hash{};
        hash.data[0] = kProgramHashFirstByte;
        return hash;
    }

    // `blobText` 为空时不建 blob（`ComPtr` 保持空）：那是"指针成员没有内容"那种对象。
    graphics_backend::ShaderLibrary makeObject(const char* blobText)
    {
        graphics_backend::ShaderLibrary lib{};
        const auto sd = makeStructData();
        lib.m_ShaderStructs.insert(castl::make_pair(cacore::NameHash("TestStruct"), sd));
        lib.m_ShaderRootStructs.insert(castl::make_pair(cacore::PathHash("RootStruct.slang"), sd));
        if (blobText == nullptr)
            return lib;

        graphics_backend::ShaderCode code{};
        code.shaderType = ECompileShaderType::eFrag;
        const std::size_t blobSize = std::strlen(blobText);
        D3DCreateBlob(blobSize, &code.data);
        std::memcpy(code.data->GetBufferPointer(), blobText, blobSize);
        graphics_backend::ShaderSourceKey sourceKey{};
        sourceKey.path = cacore::PathHash("Test.slang");
        sourceKey.entryPoint = cacore::NameHash("main");
        code.sourceKeys.insert(sourceKey);
        lib.m_ShaderPrograms.insert(castl::make_pair(programHash(), code));
        return lib;
    }

    castl::vector<unsigned char> newBytes(const graphics_backend::ShaderLibrary& lib)
    {
        castl::vector<unsigned char> bytes;
        cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::serialize(lib, bytes);
        return bytes;
    }
} // namespace

bool RunShaderLibraryBytesTest()
{
    const auto libNoProgram = makeObject(nullptr);
    const auto libWithProgram = makeObject("ABCD");

    const castl::vector<unsigned char> oldNoProgram = hexToBytes(kOldNoProgramHex);
    const castl::vector<unsigned char> oldWithProgram = hexToBytes(kOldWithProgramHex);
    const castl::vector<unsigned char> writtenNoProgram = newBytes(libNoProgram);
    const castl::vector<unsigned char> writtenWithProgram = newBytes(libWithProgram);

    // ---- ① 不含程序：逐字节相同。
    const bool sameAsOldNoProgram = sameBytes(writtenNoProgram, oldNoProgram);

    // ---- ② 含程序：新 = 老 + 插入的 12 字节（blob 的长度与内容），插入点前后逐字节对齐。
    const unsigned char expectedBlob[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          'A',  'B',  'C',  'D'};
    constexpr std::size_t expectedBlobSize = sizeof(expectedBlob);
    const std::size_t diffAt = firstDifference(writtenWithProgram, oldWithProgram);
    const bool blobIsTheOnlyDifference =
        writtenWithProgram.size() == oldWithProgram.size() + expectedBlobSize && diffAt <= oldWithProgram.size() &&
        std::memcmp(writtenWithProgram.data() + diffAt, expectedBlob, expectedBlobSize) == 0 &&
        std::memcmp(writtenWithProgram.data() + diffAt + expectedBlobSize, oldWithProgram.data() + diffAt,
                    oldWithProgram.size() - diffAt) == 0;

    // ---- ③ 往返：结构体那一半字段一致，blob 那一半不丢，再写一遍字节不变。
    graphics_backend::ShaderLibrary readBack{};
    std::size_t position = 0;
    const bool readOk =
        cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize(readBack, writtenWithProgram,
                                                                                         position) &&
        position == writtenWithProgram.size();
    const auto structIt = readBack.m_ShaderStructs.find(cacore::NameHash("TestStruct"));
    const bool structHalfOk = readOk && readBack.m_ShaderStructs.size() == 1 &&
                              structIt != readBack.m_ShaderStructs.end() &&
                              structIt->second.m_StructUniforms.m_MemorySize == 64u &&
                              structIt->second.m_StructUniforms.m_Elements.size() == 1 &&
                              structIt->second.m_Textures.size() == 1 &&
                              structIt->second.m_Textures[0].m_Name == cacore::NameHash("g_Albedo") &&
                              structIt->second.m_SubStructReferences.size() == 1 &&
                              structIt->second.m_SubStructReferences[0].m_Stride == 48u &&
                              readBack.m_ShaderRootStructs.size() == 1;
    const auto programIt = readBack.m_ShaderPrograms.find(programHash());
    const bool blobHalfOk = readOk && readBack.m_ShaderPrograms.size() == 1 &&
                            programIt != readBack.m_ShaderPrograms.end() && programIt->second.data != nullptr &&
                            programIt->second.data->GetBufferSize() == 4 &&
                            std::memcmp(programIt->second.data->GetBufferPointer(), "ABCD", 4) == 0 &&
                            programIt->second.sourceKeys.size() == 1;
    const bool rewriteIsStable = sameBytes(newBytes(readBack), writtenWithProgram);

    // ---- ④ 反向对照：同样长度、不同内容。
    const castl::vector<unsigned char> controlBytes = newBytes(makeObject("WXYZ"));
    const std::size_t controlDiffAt = firstDifference(controlBytes, writtenWithProgram);
    const bool controlDiffers = !sameBytes(controlBytes, writtenWithProgram) &&
                                controlBytes.size() == writtenWithProgram.size() && controlDiffAt == diffAt + 8;

    // ---- ⑤ 空 blob（`data` 是空指针）：不崩、写 8 字节空载荷、读回来是 0 字节的 blob、再写一遍字节不变。
    // 修之前这里是访问违例：进程以 0xC0000005 退出，最后一行输出是"探针：blob 置空后开始写"。
    auto libNullBlob = makeObject("ABCD");
    libNullBlob.m_ShaderPrograms.begin()->second.data = nullptr;
    const castl::vector<unsigned char> nullBlobBytes = newBytes(libNullBlob);
    graphics_backend::ShaderLibrary nullBlobReadBack{};
    std::size_t nullBlobPosition = 0;
    const bool nullBlobReadOk =
        cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize(nullBlobReadBack,
                                                                                         nullBlobBytes,
                                                                                         nullBlobPosition) &&
        nullBlobPosition == nullBlobBytes.size();
    const auto nullBlobIt = nullBlobReadBack.m_ShaderPrograms.find(programHash());
    const bool emptyBlobOk = nullBlobBytes.size() + 4 == writtenWithProgram.size() &&
                             !sameBytes(nullBlobBytes, writtenWithProgram) && nullBlobReadOk &&
                             nullBlobReadBack.m_ShaderPrograms.size() == 1 &&
                             nullBlobIt != nullBlobReadBack.m_ShaderPrograms.end() &&
                             nullBlobIt->second.data != nullptr && nullBlobIt->second.data->GetBufferSize() == 0 &&
                             sameBytes(newBytes(nullBlobReadBack), nullBlobBytes);

    std::printf("ShaderLibrary(D3D12) 对拍：无程序逐字节相同 = %d（新=%zu / 老=%zu），"
                "有程序只差插入的 blob = %d（新=%zu / 老=%zu，差在 %zu），"
                "读回结构体 = %d、blob = %d、重写不变 = %d，反向对照 = %d，空 blob 不崩且往返 = %d（%zu 字节）\n",
                sameAsOldNoProgram ? 1 : 0, writtenNoProgram.size(), oldNoProgram.size(),
                blobIsTheOnlyDifference ? 1 : 0, writtenWithProgram.size(), oldWithProgram.size(), diffAt,
                structHalfOk ? 1 : 0, blobHalfOk ? 1 : 0, rewriteIsStable ? 1 : 0, controlDiffers ? 1 : 0,
                emptyBlobOk ? 1 : 0, nullBlobBytes.size());

    const bool ok = sameAsOldNoProgram && blobIsTheOnlyDifference && structHalfOk && blobHalfOk &&
                    rewriteIsStable && controlDiffers && emptyBlobOk;
    if (ok)
        std::puts("CodeGen ShaderLibrary (D3D12) bytes PASSED");
    return ok;
}
