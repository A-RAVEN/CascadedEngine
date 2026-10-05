// ShaderLibrary（D3D12 后端）的生成物，在本模块的真实编译单元里被消费。
//
// 与 Vulkan 侧同一个做法：字段表与真实布局对齐、两条来源形状都实例化读路径、
// 既有哈希入口看得到生成的特化。D3D12 这条链上还有 `ComPtr<ID3DBlob>` 成员，
// 它们靠新运行时的 `wrapper_traits` 写成字节（不是指针地址）。
#include <ShaderLibrary/ShaderLibrary.h>

namespace
{
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;
    using cacore::codegen::TypeKind;
    using Library = graphics_backend::ShaderLibrary;

    static_assert(TypeDescriptor<Library>::fieldCount == 4, "ShaderLibrary 的字段表条数不是 4");
    static_assert(TypeDescriptor<Library>::fields()[0].offset == offsetof(Library, m_ShaderFiles),
                  "字段表第 0 条不是 m_ShaderFiles");
    static_assert(TypeDescriptor<Library>::fields()[1].offset == offsetof(Library, m_ShaderPrograms),
                  "字段表第 1 条不是 m_ShaderPrograms");
    static_assert(TypeDescriptor<Library>::fields()[2].offset == offsetof(Library, m_ShaderStructs),
                  "字段表第 2 条不是 m_ShaderStructs");
    static_assert(TypeDescriptor<Library>::fields()[3].offset == offsetof(Library, m_ShaderRootStructs),
                  "字段表第 3 条不是 m_ShaderRootStructs");

    static_assert(TypeDescriptor<graphics_backend::ShaderCode>::fieldCount == 3,
                  "ShaderCode 的字段表条数不是 3");
    static_assert(TypeDescriptor<graphics_backend::ShaderFileInfo>::fieldCount == 4,
                  "ShaderFileInfo 的字段表条数不是 4");
    static_assert(TypeDescriptor<graphics_backend::ShaderFileInfo::ProgramInfo>::fieldCount == 3,
                  "嵌套类型 ShaderFileInfo::ProgramInfo 的字段表条数不是 3");
    static_assert(TypeDescriptor<graphics_backend::ShaderResourceBindingInfo>::fieldCount == 10,
                  "ShaderResourceBindingInfo 的字段表条数不是 10");

    // `ComPtr<ID3DBlob>` 是包装类型：写的是它包着的字节，不是指针地址。
    static_assert(cacore::codegen::wrapper_traits<ComPtr<ID3DBlob>>::is_wrapper,
                  "ComPtr<ID3DBlob> 必须是包装类型，否则会把进程内地址写进载荷");

    static_assert(cacore::codegen::has_custom_hash_func<Library, cacore::codegen::hasher<>>,
                  "哈希入口看不到 ShaderLibrary 的生成特化");
    static_assert(cacore::codegen::has_custom_hash_func<ComPtr<ID3DBlob>, cacore::codegen::hasher<>>,
                  "哈希入口看不到 ComPtr<ID3DBlob> 的生成特化");
    // ---- 10.4：清单里另外两个 D3D12 类型同样要有字段级断言（此前只有 ShaderLibrary 一族有）。
    // 断言的是"生成表与真实定义对得上"：逐条比偏移（`offsetof`），再比容器标志与元素描述。
    using SourceKey = graphics_backend::ShaderSourceKey;
    static_assert(TypeDescriptor<SourceKey>::fieldCount == 2, "ShaderSourceKey 的字段表条数不是 2");
    static_assert(TypeDescriptor<SourceKey>::fields()[0].offset == offsetof(SourceKey, path),
                  "字段表第 0 条不是 path");
    static_assert(TypeDescriptor<SourceKey>::fields()[1].offset == offsetof(SourceKey, entryPoint),
                  "字段表第 1 条不是 entryPoint");
    static_assert(!has_flag(TypeDescriptor<SourceKey>::fields()[0].flags, FieldFlags::Container),
                  "path 是宿主类型，不是容器");

    using StructInfos = graphics_backend::StructBindingInfos;
    static_assert(TypeDescriptor<StructInfos>::fieldCount == 8, "StructBindingInfos 的字段表条数不是 8");
    static_assert(TypeDescriptor<StructInfos>::fields()[0].offset == offsetof(StructInfos, elementCount),
                  "字段表第 0 条不是 elementCount");
    static_assert(TypeDescriptor<StructInfos>::fields()[1].offset == offsetof(StructInfos, subStructOffset),
                  "字段表第 1 条不是 subStructOffset");
    static_assert(TypeDescriptor<StructInfos>::fields()[2].offset == offsetof(StructInfos, subStructCount),
                  "字段表第 2 条不是 subStructCount");
    static_assert(TypeDescriptor<StructInfos>::fields()[3].offset == offsetof(StructInfos, structBindingName),
                  "字段表第 3 条不是 structBindingName");
    static_assert(TypeDescriptor<StructInfos>::fields()[4].offset == offsetof(StructInfos, cbufferRefs),
                  "字段表第 4 条不是 cbufferRefs");
    static_assert(TypeDescriptor<StructInfos>::fields()[5].offset == offsetof(StructInfos, imageRefs),
                  "字段表第 5 条不是 imageRefs");
    static_assert(TypeDescriptor<StructInfos>::fields()[6].offset == offsetof(StructInfos, bufferRefs),
                  "字段表第 6 条不是 bufferRefs");
    static_assert(TypeDescriptor<StructInfos>::fields()[7].offset == offsetof(StructInfos, samplerRefs),
                  "字段表第 7 条不是 samplerRefs");

    // 四个 `castl::vector<uint32_t>` 必须带 Container 标志，并且元素描述是 UInt —— 少了元素描述，
    // 写出去的就只是一段没有元素语义的字节。
    static_assert(has_flag(TypeDescriptor<StructInfos>::fields()[4].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfos>::fields()[4].element != nullptr &&
                      TypeDescriptor<StructInfos>::fields()[4].element->kind == TypeKind::UInt,
                  "cbufferRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfos>::fields()[5].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfos>::fields()[5].element != nullptr &&
                      TypeDescriptor<StructInfos>::fields()[5].element->kind == TypeKind::UInt,
                  "imageRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfos>::fields()[6].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfos>::fields()[6].element != nullptr &&
                      TypeDescriptor<StructInfos>::fields()[6].element->kind == TypeKind::UInt,
                  "bufferRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfos>::fields()[7].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfos>::fields()[7].element != nullptr &&
                      TypeDescriptor<StructInfos>::fields()[7].element->kind == TypeKind::UInt,
                  "samplerRefs 必须是元素为 UInt 的容器");
} // namespace

using D3D12ShaderLibraryBytes = castl::vector<unsigned char>;
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<
    D3D12ShaderLibraryBytes>(graphics_backend::ShaderLibrary&, D3D12ShaderLibraryBytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::serialize<
    D3D12ShaderLibraryBytes>(graphics_backend::ShaderLibrary const&, D3D12ShaderLibraryBytes&);
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<ca_io::IOBatch>(
    graphics_backend::ShaderLibrary&, ca_io::IOBatch const&, std::size_t&);
