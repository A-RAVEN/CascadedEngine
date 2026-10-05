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

    static_assert(cacore::codegen::has_custom_hash_func<Library, cacore::aggregateHasher<>>,
                  "既有哈希入口看不到 ShaderLibrary 的生成特化");
    static_assert(cacore::codegen::has_custom_hash_func<ComPtr<ID3DBlob>, cacore::aggregateHasher<>>,
                  "既有哈希入口看不到 ComPtr<ID3DBlob> 的生成特化");
} // namespace

using D3D12ShaderLibraryBytes = castl::vector<unsigned char>;
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<
    D3D12ShaderLibraryBytes>(graphics_backend::ShaderLibrary&, D3D12ShaderLibraryBytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::serialize<
    D3D12ShaderLibraryBytes>(graphics_backend::ShaderLibrary const&, D3D12ShaderLibraryBytes&);
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<ca_io::IOBatch>(
    graphics_backend::ShaderLibrary&, ca_io::IOBatch const&, std::size_t&);
