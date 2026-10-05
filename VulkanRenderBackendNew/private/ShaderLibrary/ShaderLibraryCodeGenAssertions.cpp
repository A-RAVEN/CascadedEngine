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

    static_assert(cacore::codegen::has_custom_hash_func<Library, cacore::codegen::hasher<>>,
                  "哈希入口看不到 ShaderLibrary 的生成特化");
    static_assert(cacore::codegen::has_custom_hash_func<graphics_backend::VulkanShaderFileInfo,
                                                        cacore::codegen::hasher<>>,
                  "哈希入口看不到 VulkanShaderFileInfo 的生成特化");

    // ---- 10.4：清单里另外六个 Vulkan 类型也要有字段级断言（此前它们只有"被 Compiler.h include"这一档）。
    // 每个类型都逐条比偏移；`VulkanStructBindingInfo` 的四个 `vector<uint32_t>` 另外比容器标志与元素描述。
    using DescriptorBinding = graphics_backend::VulkanDescriptorBinding;
    static_assert(TypeDescriptor<DescriptorBinding>::fieldCount == 4,
                  "VulkanDescriptorBinding 的字段表条数不是 4");
    static_assert(TypeDescriptor<DescriptorBinding>::fields()[0].offset == offsetof(DescriptorBinding, binding),
                  "字段表第 0 条不是 binding");
    static_assert(TypeDescriptor<DescriptorBinding>::fields()[1].offset == offsetof(DescriptorBinding, descriptorType),
                  "字段表第 1 条不是 descriptorType");
    static_assert(TypeDescriptor<DescriptorBinding>::fields()[2].offset == offsetof(DescriptorBinding, descriptorCount),
                  "字段表第 2 条不是 descriptorCount");
    static_assert(TypeDescriptor<DescriptorBinding>::fields()[3].offset == offsetof(DescriptorBinding, stageFlags),
                  "字段表第 3 条不是 stageFlags");

    using CBufferInfo = graphics_backend::VulkanCBufferBindingInfo;
    static_assert(TypeDescriptor<CBufferInfo>::fieldCount == 5, "VulkanCBufferBindingInfo 的字段表条数不是 5");
    static_assert(TypeDescriptor<CBufferInfo>::fields()[0].offset == offsetof(CBufferInfo, spaceID),
                  "字段表第 0 条不是 spaceID");
    static_assert(TypeDescriptor<CBufferInfo>::fields()[1].offset == offsetof(CBufferInfo, elementCount),
                  "字段表第 1 条不是 elementCount");
    static_assert(TypeDescriptor<CBufferInfo>::fields()[2].offset == offsetof(CBufferInfo, bindingID),
                  "字段表第 2 条不是 bindingID");
    static_assert(TypeDescriptor<CBufferInfo>::fields()[3].offset == offsetof(CBufferInfo, usageMask),
                  "字段表第 3 条不是 usageMask");
    static_assert(TypeDescriptor<CBufferInfo>::fields()[4].offset == offsetof(CBufferInfo, cbufferStructName),
                  "字段表第 4 条不是 cbufferStructName");

    using ImageInfo = graphics_backend::VulkanImageBindingInfo;
    static_assert(TypeDescriptor<ImageInfo>::fieldCount == 7, "VulkanImageBindingInfo 的字段表条数不是 7");
    static_assert(TypeDescriptor<ImageInfo>::fields()[0].offset == offsetof(ImageInfo, resourceType),
                  "字段表第 0 条不是 resourceType");
    static_assert(TypeDescriptor<ImageInfo>::fields()[1].offset == offsetof(ImageInfo, accessType),
                  "字段表第 1 条不是 accessType");
    static_assert(TypeDescriptor<ImageInfo>::fields()[2].offset == offsetof(ImageInfo, spaceID),
                  "字段表第 2 条不是 spaceID");
    static_assert(TypeDescriptor<ImageInfo>::fields()[3].offset == offsetof(ImageInfo, elementCount),
                  "字段表第 3 条不是 elementCount");
    static_assert(TypeDescriptor<ImageInfo>::fields()[4].offset == offsetof(ImageInfo, bindingID),
                  "字段表第 4 条不是 bindingID");
    static_assert(TypeDescriptor<ImageInfo>::fields()[5].offset == offsetof(ImageInfo, usageMask),
                  "字段表第 5 条不是 usageMask");
    static_assert(TypeDescriptor<ImageInfo>::fields()[6].offset == offsetof(ImageInfo, imageBindingName),
                  "字段表第 6 条不是 imageBindingName");

    using BufferInfo = graphics_backend::VulkanBufferBindingInfo;
    static_assert(TypeDescriptor<BufferInfo>::fieldCount == 7, "VulkanBufferBindingInfo 的字段表条数不是 7");
    static_assert(TypeDescriptor<BufferInfo>::fields()[0].offset == offsetof(BufferInfo, resourceType),
                  "字段表第 0 条不是 resourceType");
    static_assert(TypeDescriptor<BufferInfo>::fields()[1].offset == offsetof(BufferInfo, accessType),
                  "字段表第 1 条不是 accessType");
    static_assert(TypeDescriptor<BufferInfo>::fields()[2].offset == offsetof(BufferInfo, spaceID),
                  "字段表第 2 条不是 spaceID");
    static_assert(TypeDescriptor<BufferInfo>::fields()[3].offset == offsetof(BufferInfo, elementCount),
                  "字段表第 3 条不是 elementCount");
    static_assert(TypeDescriptor<BufferInfo>::fields()[4].offset == offsetof(BufferInfo, bindingID),
                  "字段表第 4 条不是 bindingID");
    static_assert(TypeDescriptor<BufferInfo>::fields()[5].offset == offsetof(BufferInfo, usageMask),
                  "字段表第 5 条不是 usageMask");
    static_assert(TypeDescriptor<BufferInfo>::fields()[6].offset == offsetof(BufferInfo, bufferBindingName),
                  "字段表第 6 条不是 bufferBindingName");

    using SamplerInfo = graphics_backend::VulkanSamplerBindingInfo;
    static_assert(TypeDescriptor<SamplerInfo>::fieldCount == 5, "VulkanSamplerBindingInfo 的字段表条数不是 5");
    static_assert(TypeDescriptor<SamplerInfo>::fields()[0].offset == offsetof(SamplerInfo, spaceID),
                  "字段表第 0 条不是 spaceID");
    static_assert(TypeDescriptor<SamplerInfo>::fields()[1].offset == offsetof(SamplerInfo, elementCount),
                  "字段表第 1 条不是 elementCount");
    static_assert(TypeDescriptor<SamplerInfo>::fields()[2].offset == offsetof(SamplerInfo, bindingID),
                  "字段表第 2 条不是 bindingID");
    static_assert(TypeDescriptor<SamplerInfo>::fields()[3].offset == offsetof(SamplerInfo, usageMask),
                  "字段表第 3 条不是 usageMask");
    static_assert(TypeDescriptor<SamplerInfo>::fields()[4].offset == offsetof(SamplerInfo, samplerBindingName),
                  "字段表第 4 条不是 samplerBindingName");

    using StructInfo = graphics_backend::VulkanStructBindingInfo;
    static_assert(TypeDescriptor<StructInfo>::fieldCount == 8, "VulkanStructBindingInfo 的字段表条数不是 8");
    static_assert(TypeDescriptor<StructInfo>::fields()[0].offset == offsetof(StructInfo, elementCount),
                  "字段表第 0 条不是 elementCount");
    static_assert(TypeDescriptor<StructInfo>::fields()[1].offset == offsetof(StructInfo, subStructOffset),
                  "字段表第 1 条不是 subStructOffset");
    static_assert(TypeDescriptor<StructInfo>::fields()[2].offset == offsetof(StructInfo, subStructCount),
                  "字段表第 2 条不是 subStructCount");
    static_assert(TypeDescriptor<StructInfo>::fields()[3].offset == offsetof(StructInfo, structBindingName),
                  "字段表第 3 条不是 structBindingName");
    static_assert(TypeDescriptor<StructInfo>::fields()[4].offset == offsetof(StructInfo, cbufferRefs),
                  "字段表第 4 条不是 cbufferRefs");
    static_assert(TypeDescriptor<StructInfo>::fields()[5].offset == offsetof(StructInfo, imageRefs),
                  "字段表第 5 条不是 imageRefs");
    static_assert(TypeDescriptor<StructInfo>::fields()[6].offset == offsetof(StructInfo, bufferRefs),
                  "字段表第 6 条不是 bufferRefs");
    static_assert(TypeDescriptor<StructInfo>::fields()[7].offset == offsetof(StructInfo, samplerRefs),
                  "字段表第 7 条不是 samplerRefs");
    static_assert(has_flag(TypeDescriptor<StructInfo>::fields()[4].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfo>::fields()[4].element != nullptr &&
                      TypeDescriptor<StructInfo>::fields()[4].element->kind == cacore::codegen::TypeKind::UInt,
                  "cbufferRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfo>::fields()[5].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfo>::fields()[5].element != nullptr &&
                      TypeDescriptor<StructInfo>::fields()[5].element->kind == cacore::codegen::TypeKind::UInt,
                  "imageRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfo>::fields()[6].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfo>::fields()[6].element != nullptr &&
                      TypeDescriptor<StructInfo>::fields()[6].element->kind == cacore::codegen::TypeKind::UInt,
                  "bufferRefs 必须是元素为 UInt 的容器");
    static_assert(has_flag(TypeDescriptor<StructInfo>::fields()[7].flags, FieldFlags::Container) &&
                      TypeDescriptor<StructInfo>::fields()[7].element != nullptr &&
                      TypeDescriptor<StructInfo>::fields()[7].element->kind == cacore::codegen::TypeKind::UInt,
                  "samplerRefs 必须是元素为 UInt 的容器");
} // namespace

// ② 两条来源形状各实例化一遍：缓冲区（带长度检查）与流式批（TResource 走的那条）。
using VulkanShaderLibraryBytes = castl::vector<unsigned char>;
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<
    VulkanShaderLibraryBytes>(graphics_backend::ShaderLibrary&, VulkanShaderLibraryBytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::serialize<
    VulkanShaderLibraryBytes>(graphics_backend::ShaderLibrary const&, VulkanShaderLibraryBytes&);
template bool cacore::codegen::CodeGenSerializer<graphics_backend::ShaderLibrary>::deserialize<ca_io::IOBatch>(
    graphics_backend::ShaderLibrary&, ca_io::IOBatch const&, std::size_t&);
