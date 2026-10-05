// ShaderCompilerSlang 那 22 个类型的生成物，在本模块的真实编译单元里被消费。
//
// 断言的是"生成表与真实定义对得上"：逐条比字段数，再把每个成员的偏移与 `offsetof` 对照 ——
// `offsetof` 是编译器给出的数，独立于生成表，所以生成表少一条、多一条、顺序错了都会当场红。
// 这些类型此前只有"被 `Compiler.h` 逐个 `#include`"这一档（编不过也算拦住），按 10.4 的要求补上
// 字段级断言这一档。
#include <Compiler.h>     // 22 个类型的定义 + 它们的生成头（Compiler.h 逐个 include）
#include <CodeGenMeta.h>  // TypeDescriptor

// `Compiler.h` 只 include 了这些类型的 `.Serialization.h` / `.Hash.h`，**没有** include `.Reflection.h` ——
// 所以描述符表要自己带进来，否则 `TypeDescriptor<T>` 落在主模板上（fieldCount 0），下面每一条都会红。
#include "UniformElement.Reflection.h"
#include "ShaderStructUniforms.Reflection.h"
#include "BufferData.Reflection.h"
#include "ShaderTextureData.Reflection.h"
#include "SubStructReference.Reflection.h"
#include "ShaderStructData.Reflection.h"
#include "ShaderResourceBinding.Reflection.h"
#include "ShaderBindingHierarchy.Reflection.h"
#include "ShaderSpaceResourceStats.Reflection.h"
#include "BindingDesc.Reflection.h"
#include "BufferBindingDesc.Reflection.h"
#include "ShaderSpaceInfo.Reflection.h"
#include "ShaderBindingInfo.Reflection.h"
#include "UniformGroup.Reflection.h"
#include "UniformBufferData.Reflection.h"
#include "ShaderResourceGroups.Reflection.h"
#include "TextureData.Reflection.h"
#include "SamplerData.Reflection.h"
#include "ShaderBufferData.Reflection.h"
#include "ShaderBindingSpaceData.Reflection.h"
#include "ShaderVertexAttributeData.Reflection.h"
#include "ShaderReflectionData.Reflection.h"
#include "ShaderProgramData.Reflection.h"
#include "ShaderCompileTargetResult.Reflection.h"

namespace
{
    using cacore::codegen::TypeDescriptor;

    // 逐字符比名字。不能比指针：这里写的字面量与生成表里的字面量不保证被编译器合并。
    constexpr bool cgSameName(const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b)
        {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    template<typename T>
    constexpr std::size_t cgFieldOffset(const char* name)
    {
        for (std::size_t i = 0; i < TypeDescriptor<T>::fieldCount; ++i)
            if (cgSameName(TypeDescriptor<T>::fields()[i].name, name))
                return TypeDescriptor<T>::fields()[i].offset;
        return static_cast<std::size_t>(-1);
    }
} // namespace

// 一个成员一条：生成表里那条的偏移必须等于 offsetof；表里没有这个名字就会拿到 -1，也会红。
#define CA_OFFSET(T, member)                                                                       \
    static_assert(cgFieldOffset<T>(#member) == offsetof(T, member),                                 \
                  #T " 的成员 " #member " 在生成表里的偏移与 offsetof 不一致")

namespace
{
    static_assert(TypeDescriptor<ShaderCompilerSlang::UniformElement>::fieldCount == 6,
                  "UniformElement 的字段表条数不是 6");
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_TypeName);
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_Name);
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_MemoryOffset);
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_ElementMemorySize);
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_Stride);
    CA_OFFSET(ShaderCompilerSlang::UniformElement, m_ElementCount);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderStructUniforms>::fieldCount == 3,
                  "ShaderStructUniforms 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::ShaderStructUniforms, m_MemorySize);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructUniforms, m_Stride);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructUniforms, m_Elements);

    static_assert(TypeDescriptor<ShaderCompilerSlang::BufferData>::fieldCount == 3,
                  "BufferData 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::BufferData, m_Name);
    CA_OFFSET(ShaderCompilerSlang::BufferData, m_RWType);
    CA_OFFSET(ShaderCompilerSlang::BufferData, m_ElementCount);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderTextureData>::fieldCount == 3,
                  "ShaderTextureData 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::ShaderTextureData, m_Name);
    CA_OFFSET(ShaderCompilerSlang::ShaderTextureData, m_RWType);
    CA_OFFSET(ShaderCompilerSlang::ShaderTextureData, m_ElementCount);

    static_assert(TypeDescriptor<ShaderCompilerSlang::SubStructReference>::fieldCount == 6,
                  "SubStructReference 的字段表条数不是 6");
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_Name);
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_StructTypeName);
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_ElementCount);
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_MemoryOffset);
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_ElementMemorySize);
    CA_OFFSET(ShaderCompilerSlang::SubStructReference, m_Stride);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderStructData>::fieldCount == 6,
                  "ShaderStructData 的字段表条数不是 6");
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_TypeName);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_StructUniforms);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_Textures);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_TextureSamplers);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_Buffers);
    CA_OFFSET(ShaderCompilerSlang::ShaderStructData, m_SubStructReferences);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderResourceBinding>::fieldCount == 8,
                  "ShaderResourceBinding 的字段表条数不是 8");
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_BindingSpace);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_BindingID);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_Name);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_TypeName);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_Access);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_ResourceType);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_ElementCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceBinding, m_Usage);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderBindingHierarchy>::fieldCount == 9,
                  "ShaderBindingHierarchy 的字段表条数不是 9");
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_Name);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_TypeName);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_ElementCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_ParentID);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_SelfUniformBufferID);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_SelfUniformSpaceID);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_SelfUniformUsage);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_Bindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingHierarchy, m_SubBindingHierarchies);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderSpaceResourceStats>::fieldCount == 13,
                  "ShaderSpaceResourceStats 的字段表条数不是 13");
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_CBufferBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_SamplerBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_TextureBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_RWTextureBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_StorageBufferBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_RWBufferBindings);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_TotalBindingCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_CBufferCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_SamplerCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_TextureCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_RWTextureCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_StorageBufferCount);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceResourceStats, m_RWBufferCount);

    // 下面两个是嵌套类型，清单里按短名标注，生成表里带完整限定名 —— 断言必须写全名。
    using NestedBindingDesc = ShaderCompilerSlang::ShaderSpaceResourceStats::BindingDesc;
    static_assert(TypeDescriptor<NestedBindingDesc>::fieldCount == 2, "BindingDesc 的字段表条数不是 2");
    CA_OFFSET(NestedBindingDesc, bindingID);
    CA_OFFSET(NestedBindingDesc, elementCount);

    using NestedBufferBindingDesc = ShaderCompilerSlang::ShaderSpaceResourceStats::BufferBindingDesc;
    static_assert(TypeDescriptor<NestedBufferBindingDesc>::fieldCount == 3,
                  "BufferBindingDesc 的字段表条数不是 3");
    CA_OFFSET(NestedBufferBindingDesc, bindingID);
    CA_OFFSET(NestedBufferBindingDesc, elementCount);
    CA_OFFSET(NestedBufferBindingDesc, memoryStride);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderSpaceInfo>::fieldCount == 3,
                  "ShaderSpaceInfo 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceInfo, m_SpaceID);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceInfo, m_RootHierarchyID);
    CA_OFFSET(ShaderCompilerSlang::ShaderSpaceInfo, m_ResourceStats);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderBindingInfo>::fieldCount == 3,
                  "ShaderBindingInfo 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingInfo, m_BindingDataHierarchies);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingInfo, m_SpaceInfos);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingInfo, m_RootHierarchyID);

    static_assert(TypeDescriptor<ShaderCompilerSlang::UniformGroup>::fieldCount == 7,
                  "UniformGroup 的字段表条数不是 7");
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_Name);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_MemoryOffset);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_MemorySize);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_Stride);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_ElementCount);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_Elements);
    CA_OFFSET(ShaderCompilerSlang::UniformGroup, m_SubGroups);

    static_assert(TypeDescriptor<ShaderCompilerSlang::UniformBufferData>::fieldCount == 2,
                  "UniformBufferData 的字段表条数不是 2");
    CA_OFFSET(ShaderCompilerSlang::UniformBufferData, m_BindingIndex);
    CA_OFFSET(ShaderCompilerSlang::UniformBufferData, m_Groups);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderResourceGroups>::fieldCount == 5,
                  "ShaderResourceGroups 的字段表条数不是 5");
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceGroups, m_Name);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceGroups, m_SubGroups);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceGroups, m_Buffers);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceGroups, m_Textures);
    CA_OFFSET(ShaderCompilerSlang::ShaderResourceGroups, m_Samplers);

    static_assert(TypeDescriptor<ShaderCompilerSlang::TextureData>::fieldCount == 4,
                  "TextureData 的字段表条数不是 4");
    CA_OFFSET(ShaderCompilerSlang::TextureData, m_BindingIndex);
    CA_OFFSET(ShaderCompilerSlang::TextureData, m_Count);
    CA_OFFSET(ShaderCompilerSlang::TextureData, m_Access);
    CA_OFFSET(ShaderCompilerSlang::TextureData, m_Name);

    static_assert(TypeDescriptor<ShaderCompilerSlang::SamplerData>::fieldCount == 3,
                  "SamplerData 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::SamplerData, m_BindingIndex);
    CA_OFFSET(ShaderCompilerSlang::SamplerData, m_Count);
    CA_OFFSET(ShaderCompilerSlang::SamplerData, m_Name);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderBufferData>::fieldCount == 4,
                  "ShaderBufferData 的字段表条数不是 4");
    CA_OFFSET(ShaderCompilerSlang::ShaderBufferData, m_BindingIndex);
    CA_OFFSET(ShaderCompilerSlang::ShaderBufferData, m_Count);
    CA_OFFSET(ShaderCompilerSlang::ShaderBufferData, m_Access);
    CA_OFFSET(ShaderCompilerSlang::ShaderBufferData, m_Name);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderBindingSpaceData>::fieldCount == 6,
                  "ShaderBindingSpaceData 的字段表条数不是 6");
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_BindingSpace);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_UniformBuffers);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_Buffers);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_Textures);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_Samplers);
    CA_OFFSET(ShaderCompilerSlang::ShaderBindingSpaceData, m_ResourceGroups);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderVertexAttributeData>::fieldCount == 4,
                  "ShaderVertexAttributeData 的字段表条数不是 4");
    CA_OFFSET(ShaderCompilerSlang::ShaderVertexAttributeData, m_Name);
    CA_OFFSET(ShaderCompilerSlang::ShaderVertexAttributeData, m_SematicName);
    CA_OFFSET(ShaderCompilerSlang::ShaderVertexAttributeData, m_SematicIndex);
    CA_OFFSET(ShaderCompilerSlang::ShaderVertexAttributeData, m_Location);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderReflectionData>::fieldCount == 4,
                  "ShaderReflectionData 的字段表条数不是 4");
    CA_OFFSET(ShaderCompilerSlang::ShaderReflectionData, m_BindingData);
    CA_OFFSET(ShaderCompilerSlang::ShaderReflectionData, m_VertexAttributes);
    CA_OFFSET(ShaderCompilerSlang::ShaderReflectionData, m_ShaderStructs);
    CA_OFFSET(ShaderCompilerSlang::ShaderReflectionData, m_BindingInfo);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderProgramData>::fieldCount == 3,
                  "ShaderProgramData 的字段表条数不是 3");
    CA_OFFSET(ShaderCompilerSlang::ShaderProgramData, shaderType);
    CA_OFFSET(ShaderCompilerSlang::ShaderProgramData, data);
    CA_OFFSET(ShaderCompilerSlang::ShaderProgramData, entryPointName);

    static_assert(TypeDescriptor<ShaderCompilerSlang::ShaderCompileTargetResult>::fieldCount == 4,
                  "ShaderCompileTargetResult 的字段表条数不是 4");
    CA_OFFSET(ShaderCompilerSlang::ShaderCompileTargetResult, targetType);
    CA_OFFSET(ShaderCompilerSlang::ShaderCompileTargetResult, shaderTypeFlags);
    CA_OFFSET(ShaderCompilerSlang::ShaderCompileTargetResult, programs);
    CA_OFFSET(ShaderCompilerSlang::ShaderCompileTargetResult, m_ReflectionData);
} // namespace
