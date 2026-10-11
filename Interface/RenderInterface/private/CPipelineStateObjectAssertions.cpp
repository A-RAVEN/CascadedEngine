// CPipelineStateObject 一族的生成物，在真实引擎目标（`Rendering`）里被消费。
//
// 这里做三件事，都是编译期的：
//   ① 字段表与真实布局对得上（偏移用 `offsetof`，也就是编译器自己给的数）；
//   ② 容器的判定与字段表一致（`colorAttachments` 是 `castl::array`，定长数组没有 `clear()`）；
//   ③ **实例化**生成的读写路径 —— 出码成功只说明工具跑通了，不说明这段代码能实例化。
#include <CPipelineStateObject.h>
#include <CVertexInputDescriptor.h>
#include <ShaderResourceHandle.h>
#include <GPUGraph.h>
#include <CodeGenMeta.h>
#include <HashKeys.h>

// 生成物：这九个类型各自的 `<类型>.codegen.h`（一个类型一份，反射 / 序列化 / 哈希三节都在里面）。
// 既有调用点要用上生成的特化，本单元就必须看到**每一个**成员类型的特化。
#include "StencilStates.codegen.h"
#include "DepthStencilStates.codegen.h"
#include "RasterizerStates.codegen.h"
#include "SingleColorAttachmentBlendStates.codegen.h"
#include "ColorAttachmentsBlendStates.codegen.h"
#include "CPipelineStateObject.codegen.h"
#include "VertexAttribute.codegen.h"
#include "VertexInputsDescriptor.codegen.h"
#include "ResourceHandleKeyData.codegen.h"

namespace
{
    using cacore::codegen::CodeGenFieldInfo;
    using cacore::codegen::FieldFlags;
    using cacore::codegen::has_flag;
    using cacore::codegen::TypeDescriptor;

    constexpr const CodeGenFieldInfo* fieldOf(const CodeGenFieldInfo* fields, std::size_t count,
                                              const char* name)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (fields[i].name == name)
                return &fields[i];
        return nullptr;
    }

    // ① 顶层类型：四个成员，偏移与声明顺序都用编译器给的数核对。
    static_assert(TypeDescriptor<CPipelineStateObject>::fieldCount == 4,
                  "CPipelineStateObject 的字段表条数不是 4");
    static_assert(TypeDescriptor<CPipelineStateObject>::fields()[0].offset ==
                      offsetof(CPipelineStateObject, depthStencilStates),
                  "字段表第 0 条不是 depthStencilStates");
    static_assert(TypeDescriptor<CPipelineStateObject>::fields()[1].offset ==
                      offsetof(CPipelineStateObject, rasterizationStates),
                  "字段表第 1 条不是 rasterizationStates");
    static_assert(TypeDescriptor<CPipelineStateObject>::fields()[2].offset ==
                      offsetof(CPipelineStateObject, colorAttachments),
                  "字段表第 2 条不是 colorAttachments");
    static_assert(TypeDescriptor<CPipelineStateObject>::fields()[3].offset ==
                      offsetof(CPipelineStateObject, msCount),
                  "字段表第 3 条不是 msCount");

    // ② `colorAttachments` 自己是结构体（不是容器）；它内部那个 `castl::array` 才是定长数组成员，
    //    定长数组没有 `clear()`，读取路径必须按位置覆写。
    static_assert(!has_flag(TypeDescriptor<CPipelineStateObject>::fields()[2].flags, FieldFlags::Container),
                  "colorAttachments 是结构体成员，不是容器");
    static_assert(has_flag(TypeDescriptor<ColorAttachmentsBlendStates>::fields()[0].flags,
                           FieldFlags::Container),
                  "attachmentBlendStates 是 castl::array，字段表必须标成容器");
    static_assert(!has_flag(TypeDescriptor<CPipelineStateObject>::fields()[3].flags, FieldFlags::Container),
                  "msCount 是枚举，不是容器");
    static_assert(cacore::codegen::isContainer<decltype(ColorAttachmentsBlendStates{}.attachmentBlendStates)>::value &&
                  !cacore::codegen::hasClear<decltype(ColorAttachmentsBlendStates{}.attachmentBlendStates)>::value,
                  "castl::array 按 extent 算容器，而且没有 clear()");

    // 嵌套类型 `DepthStencilStates::StencilStates` 自己的字段表：七个成员。
    static_assert(TypeDescriptor<DepthStencilStates::StencilStates>::fieldCount == 7,
                  "嵌套类型 StencilStates 的字段表条数不是 7");

    // ③ 在真实目标里实例化生成的读写路径（定长数组成员会走到"按位置覆写"那条分支）。
    using Bytes = std::vector<unsigned char>;

    // ---- 顶点输入描述（`HashObj<VertexInputsDescriptor>` 是 D3D12 GPUGraph 那几个映射的键）：
    // 它们原先靠老驱动的结构化绑定兜底，换驱动时必须自己有生成特化，字段表要跟真实定义对齐。
    static_assert(TypeDescriptor<VertexAttribute>::fieldCount == 4, "VertexAttribute 的字段表条数不是 4");
    static_assert(TypeDescriptor<VertexAttribute>::fields()[0].offset == offsetof(VertexAttribute, offset),
                  "字段表第 0 条不是 offset");
    static_assert(TypeDescriptor<VertexAttribute>::fields()[1].offset == offsetof(VertexAttribute, format),
                  "字段表第 1 条不是 format");
    static_assert(TypeDescriptor<VertexAttribute>::fields()[2].offset == offsetof(VertexAttribute, semanticName),
                  "字段表第 2 条不是 semanticName");
    static_assert(TypeDescriptor<VertexAttribute>::fields()[3].offset == offsetof(VertexAttribute, sematicIndex),
                  "字段表第 3 条不是 sematicIndex");
    static_assert(TypeDescriptor<VertexInputsDescriptor>::fieldCount == 3,
                  "VertexInputsDescriptor 的字段表条数不是 3");
    static_assert(TypeDescriptor<VertexInputsDescriptor>::fields()[0].offset ==
                      offsetof(VertexInputsDescriptor, stride),
                  "字段表第 0 条不是 stride");
    static_assert(TypeDescriptor<VertexInputsDescriptor>::fields()[1].offset ==
                      offsetof(VertexInputsDescriptor, perInstance),
                  "字段表第 1 条不是 perInstance");
    static_assert(TypeDescriptor<VertexInputsDescriptor>::fields()[2].offset ==
                      offsetof(VertexInputsDescriptor, attributes),
                  "字段表第 2 条不是 attributes");
    static_assert(has_flag(TypeDescriptor<VertexInputsDescriptor>::fields()[2].flags, FieldFlags::Container) &&
                      TypeDescriptor<VertexInputsDescriptor>::fields()[2].element != nullptr,
                  "attributes 是 castl::vector，字段表必须标成带元素描述的容器");

    // ---- 句柄的键（`ResourceHandleKey = HashObj<ResourceHandleKeyData, FullCompare>`，也是哈希键）：
    // 它本身是值语义，走生成表；两个句柄类自己有 `shared_ptr` 成员、进不了生成表，在头里手写哈希特化。
    static_assert(TypeDescriptor<graphics_backend::ResourceHandleKeyData>::fieldCount == 3,
                  "ResourceHandleKeyData 的字段表条数不是 3");
    static_assert(TypeDescriptor<graphics_backend::ResourceHandleKeyData>::fields()[0].offset ==
                      offsetof(graphics_backend::ResourceHandleKeyData, name),
                  "字段表第 0 条不是 name");
    static_assert(TypeDescriptor<graphics_backend::ResourceHandleKeyData>::fields()[1].offset ==
                      offsetof(graphics_backend::ResourceHandleKeyData, uniqueID),
                  "字段表第 1 条不是 uniqueID");
    static_assert(TypeDescriptor<graphics_backend::ResourceHandleKeyData>::fields()[2].offset ==
                      offsetof(graphics_backend::ResourceHandleKeyData, threadID),
                  "字段表第 2 条不是 threadID");

    // ⑦ `ViewRectData`（`GPUGraph.h`）：它原来因为解析门进不了生成表、只能手写哈希特化，现在由生成物提供。
    //    它也是哈希键（`HashObj<ViewRectData>`，D3D12 的 GPUGraphExecutor 按视图矩形查表），
    //    所以字段表与哈希特化两条都要在。
    using ViewRect = graphics_backend::ViewRectData;
    static_assert(TypeDescriptor<ViewRect>::fieldCount == 4, "ViewRectData 的字段表条数不是 4");
    static_assert(TypeDescriptor<ViewRect>::fields()[0].offset == offsetof(ViewRect, x),
                  "字段表第 0 条不是 x");
    static_assert(TypeDescriptor<ViewRect>::fields()[1].offset == offsetof(ViewRect, y),
                  "字段表第 1 条不是 y");
    static_assert(TypeDescriptor<ViewRect>::fields()[2].offset == offsetof(ViewRect, width),
                  "字段表第 2 条不是 width");
    static_assert(TypeDescriptor<ViewRect>::fields()[3].offset == offsetof(ViewRect, height),
                  "字段表第 3 条不是 height");
} // namespace

// ④ 哈希调用点（本模块 `GPUGraph.h` 的 `HashObj<CPipelineStateObject>`，它内部走这个哈希对象）必须
//    用上生成的特化。这几条是编译期判据：生成头不在 include 路径上、或成员类型少一个特化，它们就编不过。
static_assert(cacore::codegen::has_custom_hash_func<CPipelineStateObject, cacore::codegen::hasher<>>,
              "本模块的哈希入口看不到 CPipelineStateObject 的生成特化");
static_assert(cacore::codegen::has_custom_hash_func<ColorAttachmentsBlendStates, cacore::codegen::hasher<>>,
              "本模块的哈希入口看不到 ColorAttachmentsBlendStates 的生成特化");
static_assert(cacore::codegen::has_custom_hash_func<VertexInputsDescriptor, cacore::codegen::hasher<>>,
              "哈希入口看不到 VertexInputsDescriptor 的生成特化");
static_assert(cacore::codegen::has_custom_hash_func<VertexAttribute, cacore::codegen::hasher<>>,
              "哈希入口看不到 VertexAttribute 的生成特化");
static_assert(cacore::codegen::has_custom_hash_func<graphics_backend::ViewRectData, cacore::codegen::hasher<>>,
              "哈希入口看不到 ViewRectData 的生成特化");

template bool cacore::codegen::CodeGenSerializer<CPipelineStateObject>::deserialize<Bytes>(
    CPipelineStateObject&, Bytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<CPipelineStateObject>::serialize<Bytes>(
    CPipelineStateObject const&, Bytes&);
template bool cacore::codegen::CodeGenSerializer<VertexInputsDescriptor>::deserialize<Bytes>(
    VertexInputsDescriptor&, Bytes const&, std::size_t&);
template void cacore::codegen::CodeGenSerializer<VertexInputsDescriptor>::serialize<Bytes>(
    VertexInputsDescriptor const&, Bytes&);
