// ShaderLibrary（Vulkan 后端）的生成物，在本模块的真实编译单元里被消费。
//
// 三件事，都是编译期的：
//   ① 字段表与真实布局对得上（偏移用 `offsetof`，也就是编译器自己给的数）；
//   ② **实例化**生成的读写路径 —— 而且是两条来源形状都实例化：缓冲区（`castl::vector<uint8_t>`）
//      与流式（`ca_io::IOBatch`，走 `TResource` 的那条路）；
//   ③ 既有哈希入口看得到生成的特化（分布式字典拿这些类型当键，看不到就会退回整块字节）。
#include <ShaderLibrary/ShaderLibrary.h>

namespace
{
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;
    using Library = graphics_backend::ShaderLibrary;

    // ① 顶层：四个无序映射，声明顺序与偏移都核对。
    static_assert(TypeDescriptor<Library>::fieldCount == 4, "ShaderLibrary 的字段表条数不是 4");
    static_assert(TypeDescriptor<Library>::fields()[0].offset == offsetof(Library, m_ShaderFiles),
                  "字段表第 0 条不是 m_ShaderFiles");
    static_assert(TypeDescriptor<Library>::fields()[1].offset == offsetof(Library, m_ShaderPrograms),
                  "字段表第 1 条不是 m_ShaderPrograms");
    static_assert(TypeDescriptor<Library>::fields()[2].offset == offsetof(Library, m_ShaderStructs),
                  "字段表第 2 条不是 m_ShaderStructs");
    static_assert(TypeDescriptor<Library>::fields()[3].offset == offsetof(Library, m_ShaderRootStructs),
                  "字段表第 3 条不是 m_ShaderRootStructs");
    static_assert(has_flag(TypeDescriptor<Library>::fields()[0].flags, FieldFlags::Unordered) &&
                  has_flag(TypeDescriptor<Library>::fields()[1].flags, FieldFlags::Unordered) &&
                  has_flag(TypeDescriptor<Library>::fields()[2].flags, FieldFlags::Unordered) &&
                  has_flag(TypeDescriptor<Library>::fields()[3].flags, FieldFlags::Unordered),
                  "四个成员都是无序映射，字段表必须逐个标成 Unordered");

    // 链上的类型：条数与键/值描述都要在。
    static_assert(TypeDescriptor<graphics_backend::VulkanShaderFileInfo>::fieldCount == 4,
                  "VulkanShaderFileInfo 的字段表条数不是 4");
    static_assert(TypeDescriptor<graphics_backend::VulkanShaderCode>::fieldCount == 2,
                  "VulkanShaderCode 的字段表条数不是 2");
    static_assert(TypeDescriptor<graphics_backend::VulkanShaderFileInfo::ProgramInfo>::fieldCount == 3,
                  "嵌套类型 VulkanShaderFileInfo::ProgramInfo 的字段表条数不是 3");

    static_assert(cacore::codegen::has_custom_hash_func<Library, cacore::aggregateHasher<>>,
                  "既有哈希入口看不到 ShaderLibrary 的生成特化");
    static_assert(cacore::codegen::has_custom_hash_func<graphics_backend::VulkanShaderFileInfo,
                                                        cacore::aggregateHasher<>>,
                  "既有哈希入口看不到 VulkanShaderFileInfo 的生成特化");
} // namespace

// ② 两条来源形状各实例化一遍：缓冲区（带长度检查）与流式批（TResource 走的那条）。
using VulkanShaderLibraryBytes = castl::vector<unsigned char>;
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<
    VulkanShaderLibraryBytes>(graphics_backend::ShaderLibrary&, VulkanShaderLibraryBytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::serialize<
    VulkanShaderLibraryBytes>(graphics_backend::ShaderLibrary const&, VulkanShaderLibraryBytes&);
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<ca_io::IOBatch>(
    graphics_backend::ShaderLibrary&, ca_io::IOBatch const&, std::size_t&);
