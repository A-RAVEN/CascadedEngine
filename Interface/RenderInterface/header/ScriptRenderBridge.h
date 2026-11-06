#pragma once
// 脚本专用窄渲染适配面（csharp-render-backend-triangle；csharp-render-shader-constants 追加参数结构；
// csharp-render-texture-sampling 追加纹理）。
//
// 这里只放**跨脚本边界**的五个纯虚接口：目标、图、后端、参数结构、纹理。它们只使用已经归档的绑定形状
// （定宽标量、公开记录、文本/路径值、同步字节、接口智能指针），不暴露 `TaskScheduler*`、
// `CRenderBackend::Release`、具体建图类或原生资源地址。
//
// 建图与提交的失败一律走 `int32_t` 状态 + `GetLastError()` 文本：0 = 成功、非 0 = 拒绝；
// 成功调用先清空旧错误。这样错误不穿过原生异常边界，也不依赖生成物内部的错误槽。
#include <stdint.h>

#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAVector.h>

#include <HashKeys.h>
#include "CodeGenMeta.h"
#include "GPUBuffer.h"
#include "TextureSampler.h"

// 窗口只以智能指针入参出现：真实窗口由 `IWindowSystem` 创建，句柄由窗口接口发行。
namespace cawindow
{
	class IWindow;
}

namespace graphics_backend
{
	// 只读纹理句柄（csharp-render-texture-sampling 任务 1.1）：纹理由后端适配器发行、由适配器保活，
// 脚本侧只读宽高。本批不提供显式销毁（释放与飞行中提交的语义留给后续批）。
	CA_BIND(7)
	class IScriptTexture
	{
	public:
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual ~IScriptTexture() = default;
	};

	CA_BIND(5)
	class IScriptRenderTarget
	{
	public:
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual bool IsBackbuffer() const = 0;
		virtual ~IScriptRenderTarget() = default;
	};

	CA_BIND(6)
	class IScriptShaderStruct
	{
	public:
		// 字段必须是该类型元数据里的 12 字节 `float3` 单元素字段（类型名与元素数都要核对：
		// 12 字节的 `int3`、`float3[2]` 的数组元素同样拒绝）；否则拒绝并点名 field。
		virtual int32_t SetFloat3(cacore::NameHash const& field, float x, float y, float z) = 0;
		// field 必须是元数据里的子结构数组，sub 的类型名必须等于该条目的 `m_StructTypeName`，
		// `elementIndex` 必须小于 `m_ElementCount`；违反逐项拒绝并点名参数。
		virtual int32_t SetStruct(cacore::NameHash const& field,
			castl::shared_ptr<IScriptShaderStruct> const& sub, uint32_t elementIndex) = 0;
		// 把**将绑定的那张图**里经 `AddBuffer` 登记的结构化缓冲按名绑到本结构的缓冲字段：
		// 字段必须在类型元数据的缓冲表内、访问类型只读、描述符数组长度为 1（数组字段留后续批）。
		// 缓冲名按"先绑后建"的原生时序于 `SubmitGraph` 提交时解析（此刻允许尚未登记）。
		// 同一字段重复绑定 MUST 成功并以最后一次为准（缓冲绑定是状态写入，与流名查重的
		// 「重复即拒绝」语义不同），有效性由提交时检查兜底。
		virtual int32_t SetBuffer(cacore::NameHash const& field,
			cacore::NameHash const& bufferName) = 0;
		// 把适配器发行的纹理按字段名绑到本结构的纹理字段：字段必须在类型元数据的纹理表内、
		// 访问类型只读、描述符数组长度为 1，纹理不能为空；违反逐项拒绝并点名参数。
		// 同一字段重复绑定以最后一次为准（与缓冲绑定同为状态写入）。
		virtual int32_t SetImage(cacore::NameHash const& field,
			castl::shared_ptr<IScriptTexture> const& texture) = 0;
		// 采样器描述按字段名绑到采样器表内的字段：字段必须在类型元数据的采样器名字表内，
		// 否则拒绝并点名 field（采样器表只有名字，没有读写类型与元素数可校验）。
		virtual int32_t SetSampler(cacore::NameHash const& field,
			TextureSamplerDescriptor const& sampler) = 0;
		virtual castl::string GetLastError() const = 0;
		virtual ~IScriptShaderStruct() = default;
	};

	CA_BIND(4)
	class IScriptRenderGraph
	{
	public:
		// 顶点字节在同步调用内复制进原生图的上传持有区；描述里的字节数必须与数组长度一致。
		virtual int32_t AddBuffer(cacore::NameHash const& name, GPUBufferDescriptor const& descriptor,
			castl::vector<uint8_t> const& data) = 0;
		// 首版只支持 POSITION / COLOR 两个 `float3` 属性；任一 `offset + 12` 越过步长即拒绝。
		virtual int32_t SetPositionColorStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t colorOffset) = 0;
		// 单 `POSITION` `float3` 流（常量色着色器只要位置）：`stride == 0` 或
		// `positionOffset + 12 > stride` 即拒绝。两种流的登记共用同一张流名表 ——
		// 重复登记同名流返回非零并点名 `streamName`（这是加严：旧行为是静默覆盖）。
		virtual int32_t SetPositionStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset) = 0;
		// 单 `POSITION` `float3` + 单 `TEXCOORD` `float2` 流：`stride == 0`、
		// `positionOffset + 12 > stride`、`texcoordOffset + 8 > stride` 即拒绝。
		// `TEXCOORD` 的顶点格式是 `eR32G32_SFloat`（2 个 float = 8 字节），不是 12 字节的三分量格式。
		// 三种流的登记共用同一张流名表 —— 重复登记同名流返回非零并点名 `streamName`。
		virtual int32_t SetPositionTextureStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t texcoordOffset) = 0;
		// 把参数结构绑定到第 passIndex 个 pass 的参数块名。passIndex 越界、参数为空、
		// blockName 不在该 pass 着色器根结构、块类型与参数结构类型名不符都拒绝并点名参数。
		// 形参名不叫 `params`：那是 C# 关键字，绑定发射器会把形参名原样搬进托管表面（构建期实测
		// `IScriptRenderGraph.Bindings.cs` 报 CS1041「params 是关键字」）。错误文本里仍点名 "params"。
		virtual int32_t SetPassParam(uint32_t passIndex, cacore::NameHash const& blockName,
			castl::shared_ptr<IScriptShaderStruct> const& parameters) = 0;
		virtual int32_t AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target,
			cacore::PathHash const& shaderPath, cacore::NameHash const& streamName,
			cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green,
			float blue, float alpha) = 0;
		// 只接受后台缓冲目标；本切片不设呈现目标数量上限，示例只用一个窗口。
		virtual int32_t Present(castl::shared_ptr<IScriptRenderTarget> const& target) = 0;
		virtual uint32_t GetPassCount() const = 0;
		virtual castl::string GetLastError() const = 0;
		virtual ~IScriptRenderGraph() = default;
	};

	CA_BIND(3)
	class IScriptRenderBackend
	{
	public:
		virtual castl::shared_ptr<IScriptRenderTarget> GetWindowTarget(
			castl::shared_ptr<cawindow::IWindow> const& window) = 0;
		virtual castl::shared_ptr<IScriptRenderTarget> CreateCaptureTarget(uint32_t width,
			uint32_t height) = 0;
		virtual castl::shared_ptr<IScriptRenderGraph> CreateGraph() = 0;
		// 一次调用完成「创建 RGBA8 纹理 → 图内上传字节 → 转采样态 → 同步等待」。
		// 字节数必须等于 `GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM)`（64 位）；
		// 零尺寸、字节数不符、重名、底层创建失败都 MUST 返回空句柄并由 `GetLastError()` 点名原因。
		// 上传的执行与等待**不**计入脚本提交路径的 `GetExecuteGraphCountForTest`/`GetWaitIdleCountForTest`。
		virtual castl::shared_ptr<IScriptTexture> CreateTexture(cacore::NameHash const& name,
			uint32_t width, uint32_t height, castl::vector<uint8_t> const& rgba) = 0;
		// 按类型名创建参数结构。未知类型 MUST 返回空句柄并由 `GetLastError()` 点名 `structTypeName`，
		// 且 MUST NOT 调用 `CRenderBackend::CreateShaderStruct`（D3D12 侧对空元数据解引用）。
		virtual castl::shared_ptr<IScriptShaderStruct> CreateShaderStruct(
			cacore::NameHash const& structTypeName) = 0;
		virtual int32_t SubmitGraph(castl::shared_ptr<IScriptRenderGraph> const& graph) = 0;
		virtual void WaitIdle() = 0;
		virtual castl::string GetLastError() const = 0;
		virtual ~IScriptRenderBackend() = default;
	};
}
