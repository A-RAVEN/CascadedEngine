// 脚本窄渲染适配面的原生实现（csharp-render-backend-triangle 任务 2.1 / 2.2）。
//
// 所有拒绝都发生在进入具体后端之前：参数名原样写进 `GetLastError()`，调用方据此定位。
// 成功调用先清空旧错误 —— 否则"上一次失败"会一直挂在这个对象上，判据分不清哪次调用出的错。
#include "ScriptRenderBridge.h"

#include <CRenderBackend.h>
#include <ThreadManager.h>

#include <string>
#include <utility>

namespace graphics_backend
{
	namespace
	{
		// POSITION / COLOR 两个 `float3` 语义各占 12 字节（design 二）。
		constexpr uint32_t kFloat3ByteSize = 12u;
		// TEXCOORD 是 `float2`：8 字节（csharp-render-texture-sampling 任务 2.3）。
		constexpr uint32_t kFloat2ByteSize = 8u;
		// Slang 的 `float3` 完整类型名（实测：`CAAssets/D3D12ShaderLibrary.shLib` 里 `ColorSubStruct`
		// 的元素表逐字是 `vector<float,3>`，那是 `TypeReflection::getFullName()` 的输出）。
		// 只按字节数（12）挡不住 `int3` 与 `float3[2]` 的数组元素，所以类型名与元素数都要核。
		constexpr char const* kSlangFloat3TypeName = "vector<float,3>";
		// `ShaderStruct::SetValue` 按 `sizeof(T)` 写字节：这个 POD 恰好 12 字节，与着色器里的
		// `float3` 同布局，不必把 glm 拉进渲染接口模块。
		struct Float3Value
		{
			float x;
			float y;
			float z;
		};
		static_assert(sizeof(Float3Value) == kFloat3ByteSize, "float3 必须是 12 字节");
	}

	// ---- CScriptRenderTarget ----

	CScriptRenderTarget::CScriptRenderTarget(ImageHandle image, GPUTextureDescriptor const& descriptor,
		bool backbuffer)
		: m_Image(std::move(image))
		, m_Descriptor(descriptor)
		, m_Backbuffer(backbuffer)
	{
	}

	uint32_t CScriptRenderTarget::GetWidth() const
	{
		return m_Descriptor.width;
	}

	uint32_t CScriptRenderTarget::GetHeight() const
	{
		return m_Descriptor.height;
	}

	bool CScriptRenderTarget::IsBackbuffer() const
	{
		return m_Backbuffer;
	}

	// ---- CScriptTexture ----

	CScriptTexture::CScriptTexture(uint32_t width, uint32_t height,
		castl::shared_ptr<GPUTexture> nativeTexture)
		: m_Width(width)
		, m_Height(height)
		, m_NativeTexture(std::move(nativeTexture))
	{
	}

	uint32_t CScriptTexture::GetWidth() const
	{
		return m_Width;
	}

	uint32_t CScriptTexture::GetHeight() const
	{
		return m_Height;
	}

	// ---- CScriptShaderStruct ----

	CScriptShaderStruct::CScriptShaderStruct(cacore::NameHash const& typeName,
		ShaderCompilerSlang::ShaderStructData const* data, castl::shared_ptr<ShaderStruct> shaderStruct,
		ScriptShaderCallCounters* counters)
		: m_TypeName(typeName)
		, m_Data(data)
		, m_ShaderStruct(std::move(shaderStruct))
		, m_Counters(counters)
	{
	}

	int32_t CScriptShaderStruct::Fail(char const* parameter, char const* reason)
	{
		m_LastError = castl::string("IScriptShaderStruct.") +
			(parameter == nullptr ? "?" : parameter) + "：" + (reason == nullptr ? "参数无效" : reason);
		return 1;
	}

	int32_t CScriptShaderStruct::SetFloat3(cacore::NameHash const& field, float x, float y, float z)
	{
		if (!field.Valid())
		{
			return Fail("field", "字段名不能为空");
		}
		if (m_Data == nullptr || m_ShaderStruct == nullptr)
		{
			return Fail("field", "参数结构没有编译元数据或真实对象");
		}
		ShaderCompilerSlang::UniformElement const* element = nullptr;
		for (auto const& candidate : m_Data->m_StructUniforms.m_Elements)
		{
			if (candidate.m_Name == field)
			{
				element = &candidate;
				break;
			}
		}
		if (element == nullptr)
		{
			return Fail("field", (castl::string("字段不在类型 ") + m_TypeName.c_str() + " 的元数据里：" +
				field.c_str()).c_str());
		}
		// 三条都要核：字节数、类型名、元素数。12 字节的 `int3` 与 `float3[2]` 的元素也过不了。
		if (element->m_ElementMemorySize != kFloat3ByteSize)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 必须是 12 字节的类型，实际 " +
				std::to_string(element->m_ElementMemorySize).c_str() + " 字节").c_str());
		}
		if (element->m_TypeName != cacore::NameHash(kSlangFloat3TypeName))
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() + " 类型不符：期望 " +
				kSlangFloat3TypeName + "，实际 " + element->m_TypeName.c_str()).c_str());
		}
		if (element->m_ElementCount != 1u)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() + " 必须是单元素，实际元素数 " +
				std::to_string(element->m_ElementCount).c_str()).c_str());
		}
		m_ShaderStruct->SetValue(field, Float3Value{ x, y, z }, 0u);
		if (m_Counters != nullptr)
		{
			++m_Counters->setFloat3;
		}
		ClearError();
		return 0;
	}

	int32_t CScriptShaderStruct::SetStruct(cacore::NameHash const& field,
		castl::shared_ptr<IScriptShaderStruct> const& sub, uint32_t elementIndex)
	{
		if (!field.Valid())
		{
			return Fail("field", "字段名不能为空");
		}
		if (sub == nullptr)
		{
			return Fail("sub", "子结构不能为空");
		}
		if (m_Data == nullptr || m_ShaderStruct == nullptr)
		{
			return Fail("field", "参数结构没有编译元数据或真实对象");
		}
		auto* concrete = dynamic_cast<CScriptShaderStruct*>(sub.get());
		if (concrete == nullptr || concrete->GetShaderStructForTest() == nullptr)
		{
			return Fail("sub", "子结构不是本适配面创建的参数结构");
		}
		ShaderCompilerSlang::SubStructReference const* reference = nullptr;
		for (auto const& candidate : m_Data->m_SubStructReferences)
		{
			if (candidate.m_Name == field)
			{
				reference = &candidate;
				break;
			}
		}
		if (reference == nullptr)
		{
			return Fail("field", (castl::string("子结构字段不在类型 ") + m_TypeName.c_str() +
				" 的元数据里：" + field.c_str()).c_str());
		}
		if (concrete->GetTypeNameForTest() != reference->m_StructTypeName)
		{
			return Fail("sub", (castl::string("子结构类型不符：期望 ") +
				reference->m_StructTypeName.c_str() + "，实际 " +
				concrete->GetTypeNameForTest().c_str()).c_str());
		}
		// 下标必须小于该子结构数组声明的元素数（越界在 D3D12 侧只 `CA_ASSERT`，数据会被悄悄写坏）。
		if (elementIndex >= reference->m_ElementCount)
		{
			return Fail("elementIndex", (castl::string("下标越界：元素数 ") +
				std::to_string(reference->m_ElementCount).c_str() + "，实际下标 " +
				std::to_string(elementIndex).c_str()).c_str());
		}
		// csharp-render-structured-buffer 任务 2.2：含缓冲字段的子结构会绕过顶层提交检查
		//（提交检查只走顶层类型自己的 `m_Buffers`），本批只支持顶层缓冲字段，必须拒绝。
		// csharp-render-texture-sampling 任务 2.2：纹理与采样器表同理 —— 三张资源表任一非空都拒，
		// 否则子结构里的纹理字段不会被提交检查覆盖。
		ShaderCompilerSlang::ShaderStructData const* subData = concrete->GetDataForTest();
		if (subData != nullptr && !subData->m_Buffers.empty())
		{
			return Fail("field", ((castl::string("子结构类型 ") + concrete->GetTypeNameForTest().c_str() +
				" 含缓冲字段，本批只支持顶层缓冲字段：") + field.c_str()).c_str());
		}
		if (subData != nullptr && !subData->m_Textures.empty())
		{
			return Fail("field", ((castl::string("子结构类型 ") + concrete->GetTypeNameForTest().c_str() +
				" 含纹理字段，本批只支持顶层纹理字段：") + field.c_str()).c_str());
		}
		if (subData != nullptr && !subData->m_TextureSamplers.empty())
		{
			return Fail("field", ((castl::string("子结构类型 ") + concrete->GetTypeNameForTest().c_str() +
				" 含采样器字段，本批只支持顶层采样器字段：") + field.c_str()).c_str());
		}
		m_ShaderStruct->SetStruct(field, concrete->GetShaderStructForTest(), elementIndex);
		if (m_Counters != nullptr)
		{
			++m_Counters->setStruct;
		}
		ClearError();
		return 0;
	}

	castl::string CScriptShaderStruct::GetLastError() const
	{
		return m_LastError;
	}

	// csharp-render-structured-buffer 任务 2.1：结构化缓冲字段绑定。字段级校验当场做，
	// 缓冲名只登记不解析（原生链就是先 `SetBuffer` 后 `AllocBuffer`）。
	int32_t CScriptShaderStruct::SetBuffer(cacore::NameHash const& field,
		cacore::NameHash const& bufferName)
	{
		if (!field.Valid())
		{
			return Fail("field", "字段名不能为空");
		}
		if (!bufferName.Valid())
		{
			return Fail("bufferName", "缓冲名不能为空");
		}
		if (m_Data == nullptr || m_ShaderStruct == nullptr)
		{
			return Fail("field", "参数结构没有编译元数据或真实对象");
		}
		ShaderCompilerSlang::BufferData const* bufferData = nullptr;
		for (auto const& candidate : m_Data->m_Buffers)
		{
			if (candidate.m_Name == field)
			{
				bufferData = &candidate;
				break;
			}
		}
		if (bufferData == nullptr)
		{
			return Fail("field", (castl::string("字段不在类型 ") + m_TypeName.c_str() +
				" 的缓冲元数据里：" + field.c_str()).c_str());
		}
		// `m_RWType` 的类型是 `EShaderResourceAccess`（`eUnknown`/`eReadOnly`/`eWriteOnly`/`eReadWrite`）；
		// `eStructuredBuffer` 属另一个枚举 `EShaderResourceType`，不是这里的合法取值（写错编译不过）。
		if (bufferData->m_RWType != ShaderCompilerSlang::EShaderResourceAccess::eReadOnly)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 的访问类型不是只读；读写缓冲属计算批，本批不支持").c_str());
		}
		// `m_ElementCount` 是描述符数组长度（非数组字段恒为 1、`field[N]` 为 N），不是缓冲内元素数。
		if (bufferData->m_ElementCount != 1u)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 必须是单元素（描述符数组长度为 1），实际 " +
				std::to_string(bufferData->m_ElementCount).c_str()).c_str());
		}
		// 成功：先转发给真实 `ShaderStruct`（`BufferHandle` 的本体就是名字值，缓冲允许尚未在图里
		// 登记 —— 先绑后建），再记录窄对象映射。同一字段重复绑定覆盖写入，提交解析取最后一次。
		m_ShaderStruct->SetBuffer(field, BufferHandle(bufferName), 0u);
		m_BoundBuffers[field] = bufferName;
		if (m_Counters != nullptr)
		{
			++m_Counters->setBuffer;
		}
		ClearError();
		return 0;
	}

	bool CScriptShaderStruct::HasBoundBuffer(cacore::NameHash const& field) const
	{
		return m_BoundBuffers.find(field) != m_BoundBuffers.end();
	}

	cacore::NameHash CScriptShaderStruct::GetBoundBufferName(cacore::NameHash const& field) const
	{
		auto found = m_BoundBuffers.find(field);
		return found == m_BoundBuffers.end() ? cacore::NameHash() : found->second;
	}

	// csharp-render-texture-sampling 任务 2.2：纹理字段绑定。校验全部在适配层做完 —— 两后端对
	// 未知字段静默跳过、对越界下标只 `CA_ASSERT`，脚本面不能落到那条路径上。
	// "已释放"不在这里判（本批无显式销毁、适配层不可观测，D4）：防线在托管表面对象。
	int32_t CScriptShaderStruct::SetImage(cacore::NameHash const& field,
		castl::shared_ptr<IScriptTexture> const& texture)
	{
		if (!field.Valid())
		{
			return Fail("field", "字段名不能为空");
		}
		if (texture == nullptr)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 的纹理是空句柄；纹理必须先经 CreateTexture 创建").c_str());
		}
		if (m_Data == nullptr || m_ShaderStruct == nullptr)
		{
			return Fail("field", "参数结构没有编译元数据或真实对象");
		}
		ShaderCompilerSlang::ShaderTextureData const* textureData = nullptr;
		for (auto const& candidate : m_Data->m_Textures)
		{
			if (candidate.m_Name == field)
			{
				textureData = &candidate;
				break;
			}
		}
		if (textureData == nullptr)
		{
			return Fail("field", (castl::string("字段不在类型 ") + m_TypeName.c_str() +
				" 的纹理元数据里：" + field.c_str()).c_str());
		}
		if (textureData->m_RWType != ShaderCompilerSlang::EShaderResourceAccess::eReadOnly)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 的访问类型不是只读；读写纹理（RWTexture）属计算批，本批不支持").c_str());
		}
		// `m_ElementCount` 是描述符数组长度（`Texture2D x[N]` 为 N），不是纹理尺寸。
		if (textureData->m_ElementCount != 1u)
		{
			return Fail("field", (castl::string("字段 ") + field.c_str() +
				" 必须是单元素（描述符数组长度为 1），实际 " +
				std::to_string(textureData->m_ElementCount).c_str()).c_str());
		}
		auto* concrete = dynamic_cast<CScriptTexture const*>(texture.get());
		if (concrete == nullptr || concrete->GetNativeTextureForTest() == nullptr)
		{
			return Fail("field", "纹理不是本适配面创建的对象");
		}
		// 先转发给真实 `ShaderStruct`，再记录窄对象映射（同一字段覆盖写入，提交解析取最后一次）。
		m_ShaderStruct->SetImage(field, concrete->GetNativeTextureForTest(), 0u);
		m_BoundImages[field] = texture;
		if (m_Counters != nullptr)
		{
			++m_Counters->setImage;
		}
		ClearError();
		return 0;
	}

	// 采样器名字表只有名字（`m_TextureSamplers` 是 `vector<NameHash>`）：能校验的只有存在性。
	int32_t CScriptShaderStruct::SetSampler(cacore::NameHash const& field,
		TextureSamplerDescriptor const& sampler)
	{
		if (!field.Valid())
		{
			return Fail("field", "字段名不能为空");
		}
		if (m_Data == nullptr || m_ShaderStruct == nullptr)
		{
			return Fail("field", "参数结构没有编译元数据或真实对象");
		}
		bool found = false;
		for (auto const& candidate : m_Data->m_TextureSamplers)
		{
			if (candidate == field)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return Fail("field", (castl::string("字段不在类型 ") + m_TypeName.c_str() +
				" 的采样器元数据里：" + field.c_str()).c_str());
		}
		m_ShaderStruct->SetSampler(field, sampler, 0u);
		m_BoundSamplers[field] = sampler;
		if (m_Counters != nullptr)
		{
			++m_Counters->setSampler;
		}
		ClearError();
		return 0;
	}

	bool CScriptShaderStruct::HasBoundImage(cacore::NameHash const& field) const
	{
		return m_BoundImages.find(field) != m_BoundImages.end();
	}

	castl::shared_ptr<IScriptTexture> CScriptShaderStruct::GetBoundImage(
		cacore::NameHash const& field) const
	{
		auto found = m_BoundImages.find(field);
		return found == m_BoundImages.end() ? nullptr : found->second;
	}

	bool CScriptShaderStruct::HasBoundSampler(cacore::NameHash const& field) const
	{
		return m_BoundSamplers.find(field) != m_BoundSamplers.end();
	}

	TextureSamplerDescriptor CScriptShaderStruct::GetBoundSampler(cacore::NameHash const& field) const
	{
		auto found = m_BoundSamplers.find(field);
		return found == m_BoundSamplers.end() ? TextureSamplerDescriptor{} : found->second;
	}

	// ---- CScriptRenderGraph ----

	CScriptRenderGraph::CScriptRenderGraph(CRenderBackend* backend, ScriptShaderCallCounters* counters)
		: m_Backend(backend)
		, m_Counters(counters)
		, m_Graph(castl::make_shared<GPUGraph>())
	{
	}

	int32_t CScriptRenderGraph::Fail(char const* parameter, char const* reason)
	{
		m_LastError = castl::string("IScriptRenderGraph.") + (parameter == nullptr ? "?" : parameter) +
			"：" + (reason == nullptr ? "参数无效" : reason);
		return 1;
	}

	int32_t CScriptRenderGraph::AddBuffer(cacore::NameHash const& name,
		GPUBufferDescriptor const& descriptor, castl::vector<uint8_t> const& data)
	{
		if (!name.Valid())
		{
			return Fail("name", "缓冲名称不能为空");
		}
		if (descriptor.count == 0 || descriptor.stride == 0)
		{
			return Fail("descriptor", "缓冲描述 count / stride 必须非零");
		}
		if (descriptor.count > (~static_cast<uint64_t>(0)) / descriptor.stride)
		{
			return Fail("descriptor", "缓冲描述的 count * stride 溢出");
		}
		if (m_Buffers.find(name) != m_Buffers.end())
		{
			return Fail("name", "同一张图里缓冲名称必须唯一");
		}
		const uint64_t expectedBytes = descriptor.SizeInByte();
		if (expectedBytes != static_cast<uint64_t>(data.size()))
		{
			return Fail("data", "字节数组长度与描述里的 SizeInByte 不一致");
		}
		// 同步复制：`ScheduleData` 默认 `copy = true`，原生上传持有区当场取得字节副本。
		BufferHandle bufferHandle(name);
		m_Graph->AllocBuffer(bufferHandle, descriptor);
		m_Graph->ScheduleData(bufferHandle, data.data(), static_cast<uint64_t>(data.size()), 0u, true);
		m_Buffers[name] = BufferRecord{ descriptor };
		if (m_Counters != nullptr)
		{
			++m_Counters->addBuffer;
		}
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::SetPositionColorStream(cacore::NameHash const& streamName,
		uint32_t stride, uint32_t positionOffset, uint32_t colorOffset)
	{
		if (!streamName.Valid())
		{
			return Fail("streamName", "顶点流名称不能为空");
		}
		if (positionOffset > stride || stride - positionOffset < kFloat3ByteSize)
		{
			return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
		}
		if (colorOffset > stride || stride - colorOffset < kFloat3ByteSize)
		{
			return Fail("colorOffset", "color 属性越过步长（offset + 12 > stride）");
		}
		// 流名唯一性（csharp-render-shader-constants 任务 2.1）：现状是 `m_Streams[name] = record` 静默
		// 覆盖，改判为拒绝 —— 否则调用方以为登记了两个流，实际只剩最后那一个。
		if (m_Streams.find(streamName) != m_Streams.end())
		{
			return Fail("streamName", "同一张图里流名必须唯一（这次是重复登记）");
		}
		StreamRecord record;
		record.stride = stride;
		record.positionOffset = positionOffset;
		record.colorOffset = colorOffset;
		record.vertices = VertexInputsDescriptor::Create(stride,
			{
				VertexAttribute::Create(positionOffset, VertexInputFormat::eR32G32B32_SFloat,
					CANAME("POSITION")),
				VertexAttribute::Create(colorOffset, VertexInputFormat::eR32G32B32_SFloat,
					CANAME("COLOR")),
			},
			false);
		m_Streams[streamName] = record;
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::SetPositionStream(cacore::NameHash const& streamName, uint32_t stride,
		uint32_t positionOffset)
	{
		if (!streamName.Valid())
		{
			return Fail("streamName", "顶点流名称不能为空");
		}
		if (stride == 0u)
		{
			return Fail("stride", "步长必须非零");
		}
		if (positionOffset > stride || stride - positionOffset < kFloat3ByteSize)
		{
			return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
		}
		if (m_Streams.find(streamName) != m_Streams.end())
		{
			return Fail("streamName", "同一张图里流名必须唯一（这次是重复登记）");
		}
		StreamRecord record;
		record.stride = stride;
		record.positionOffset = positionOffset;
		// 单 `POSITION` 流：常量色着色器只要位置，第二个属性留空（`colorOffset` = 0 且不参与描述创建）。
		record.colorOffset = 0u;
		record.vertices = VertexInputsDescriptor::Create(stride,
			{
				VertexAttribute::Create(positionOffset, VertexInputFormat::eR32G32B32_SFloat,
					CANAME("POSITION")),
			},
			false);
		m_Streams[streamName] = record;
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::SetPositionTextureStream(cacore::NameHash const& streamName,
		uint32_t stride, uint32_t positionOffset, uint32_t texcoordOffset)
	{
		if (!streamName.Valid())
		{
			return Fail("streamName", "顶点流名称不能为空");
		}
		if (stride == 0u)
		{
			return Fail("stride", "步长必须非零");
		}
		if (positionOffset > stride || stride - positionOffset < kFloat3ByteSize)
		{
			return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
		}
		// TEXCOORD 是 `float2` = 8 字节：和 POSITION / COLOR 的 12 字节判据不同，写成 12 会把
		// 越过步长的流放进来（原生测试正是用 `eR32G32B32_SFloat` 声明 `float2` 的越界写法）。
		if (texcoordOffset > stride || stride - texcoordOffset < kFloat2ByteSize)
		{
			return Fail("texcoordOffset", "texcoord 属性越过步长（offset + 8 > stride）");
		}
		if (m_Streams.find(streamName) != m_Streams.end())
		{
			return Fail("streamName", (castl::string("同一张图里流名必须唯一（这次是重复登记）：") +
				streamName.c_str()).c_str());
		}
		StreamRecord record;
		record.stride = stride;
		record.positionOffset = positionOffset;
		record.colorOffset = 0u;
		record.texcoordOffset = texcoordOffset;
		record.vertices = VertexInputsDescriptor::Create(stride,
			{
				VertexAttribute::Create(positionOffset, VertexInputFormat::eR32G32B32_SFloat,
					CANAME("POSITION")),
				// 8 字节的两分量格式，不是 12 字节的 `eR32G32B32_SFloat`。
				VertexAttribute::Create(texcoordOffset, VertexInputFormat::eR32G32_SFloat,
					CANAME("TEXCOORD")),
			},
			false);
		m_Streams[streamName] = record;
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::SetPassParam(uint32_t passIndex, cacore::NameHash const& blockName,
		castl::shared_ptr<IScriptShaderStruct> const& parameters)
	{
		if (parameters == nullptr)
		{
			return Fail("params", "参数结构不能为空");
		}
		if (passIndex >= GetPassCount())
		{
			return Fail("passIndex", (castl::string("pass 下标越界：pass 数 ") +
				std::to_string(GetPassCount()).c_str() + "，实际下标 " +
				std::to_string(passIndex).c_str()).c_str());
		}
		auto* concrete = dynamic_cast<CScriptShaderStruct*>(parameters.get());
		if (concrete == nullptr || concrete->GetShaderStructForTest() == nullptr)
		{
			return Fail("params", "参数结构不是本适配面创建的对象");
		}
		// 着色器路径从真实 pass 的 pipeline states 读回（`AddTrianglePass` 当场 AddPass，不延迟建图）。
		auto const& pass = m_Graph->GetRenderPasses()[passIndex];
		cacore::PathHash const shaderPath = pass.GetPipelineStates().m_ShaderInfo.path;
		if (m_Backend == nullptr)
		{
			return Fail("shaderPath", "渲染后端不可用，无法查着色器根结构");
		}
		ShaderCompilerSlang::ShaderStructData const* rootStruct =
			m_Backend->FindShaderRootStructData(shaderPath);
		if (rootStruct == nullptr)
		{
			return Fail("shaderPath", (castl::string("着色器路径没有登记的根结构：") +
				shaderPath.c_str()).c_str());
		}
		ShaderCompilerSlang::SubStructReference const* reference = nullptr;
		for (auto const& candidate : rootStruct->m_SubStructReferences)
		{
			if (candidate.m_Name == blockName)
			{
				reference = &candidate;
				break;
			}
		}
		if (reference == nullptr)
		{
			return Fail("blockName", (castl::string("参数块不在该 pass 着色器的根结构里：") +
				blockName.c_str()).c_str());
		}
		if (concrete->GetTypeNameForTest() != reference->m_StructTypeName)
		{
			return Fail("params", (castl::string("参数块类型不符：期望 ") +
				reference->m_StructTypeName.c_str() + "，实际 " +
				concrete->GetTypeNameForTest().c_str()).c_str());
		}
		// 写入真实 pass（design 第二节：`GPUGraph` 的受控可变访问器）。图与被绑定的参数结构由
		// pass 的 `shaderStructs` 持有强引用，提交后仍然保活到 `WaitIdle`。
		m_Graph->GetRenderPassForWrite(passIndex).SetParam(blockName, concrete->GetShaderStructForTest());
		// csharp-render-structured-buffer 任务 2.2：图侧登记表。`SubmitGraph` 的缓冲检查从这里拿
		// 「pass/块 → 窄参数结构」的强引用；同一 (passIndex, blockName) 重复绑定覆盖，取最后一次。
		castl::shared_ptr<CScriptShaderStruct> concreteShared =
			std::static_pointer_cast<CScriptShaderStruct>(parameters);
		bool replaced = false;
		for (auto& record : m_PassParams)
		{
			if (record.passIndex == passIndex && record.blockName == blockName)
			{
				record.parameters = concreteShared;
				replaced = true;
				break;
			}
		}
		if (!replaced)
		{
			m_PassParams.push_back(PassParamRecord{ passIndex, blockName, concreteShared });
		}
		if (m_Counters != nullptr)
		{
			++m_Counters->setPassParam;
		}
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target,
		cacore::PathHash const& shaderPath, cacore::NameHash const& streamName,
		cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green,
		float blue, float alpha)
	{
		if (target == nullptr)
		{
			return Fail("target", "渲染目标不能为空");
		}
		if (!shaderPath.Valid())
		{
			return Fail("shaderPath", "着色器路径不能为空");
		}
		auto stream = m_Streams.find(streamName);
		if (stream == m_Streams.end())
		{
			return Fail("streamName", "顶点流没有登记过");
		}
		auto buffer = m_Buffers.find(bufferName);
		if (buffer == m_Buffers.end())
		{
			return Fail("bufferName", "缓冲没有登记过");
		}
		if (vertexCount == 0)
		{
			return Fail("vertexCount", "绘制顶点数必须非零");
		}
		if (static_cast<uint64_t>(vertexCount) > buffer->second.descriptor.count)
		{
			return Fail("vertexCount", "绘制顶点数超过缓冲描述的元素数");
		}
		auto* concrete = dynamic_cast<CScriptRenderTarget*>(target.get());
		if (concrete == nullptr)
		{
			return Fail("target", "目标不是本适配面创建的对象");
		}
		RenderPass pass = RenderPass::New(concrete->GetImageForTest(),
				AttachmentConfig::Clear(GraphicsClearValue::ClearColor(red, green, blue, alpha)))
			.SetShaderInfo({ shaderPath })
			.Batch(DrawCallBatch::New()
				.VertexStream(streamName, stream->second.vertices)
				.DrawCall(DrawCall::New()
					.SetVertexBuffer(bufferName, BufferHandle(bufferName))
					.Draw(vertexCount)));
		m_Graph->AddPass(pass);
		ClearError();
		return 0;
	}

	int32_t CScriptRenderGraph::Present(castl::shared_ptr<IScriptRenderTarget> const& target)
	{
		if (target == nullptr)
		{
			return Fail("target", "渲染目标不能为空");
		}
		auto* concrete = dynamic_cast<CScriptRenderTarget*>(target.get());
		if (concrete == nullptr)
		{
			return Fail("target", "目标不是本适配面创建的对象");
		}
		if (!concrete->IsBackbuffer())
		{
			return Fail("target", "Present 只接受后台缓冲目标");
		}
		m_Graph->Present(concrete->GetImageForTest());
		ClearError();
		return 0;
	}

	uint32_t CScriptRenderGraph::GetPassCount() const
	{
		return static_cast<uint32_t>(m_Graph->GetRenderPasses().size());
	}

	castl::string CScriptRenderGraph::GetLastError() const
	{
		return m_LastError;
	}

	bool CScriptRenderGraph::HasBuffer(cacore::NameHash const& name) const
	{
		return m_Buffers.find(name) != m_Buffers.end();
	}

	cacore::NameHash CScriptRenderGraph::GetBoundBufferNameForTest(uint32_t passIndex,
		cacore::NameHash const& blockName, cacore::NameHash const& field) const
	{
		for (auto it = m_PassParams.rbegin(); it != m_PassParams.rend(); ++it)
		{
			if (it->passIndex == passIndex && it->blockName == blockName)
			{
				return it->parameters == nullptr ? cacore::NameHash()
					: it->parameters->GetBoundBufferName(field);
			}
		}
		return cacore::NameHash();
	}

	int32_t CScriptRenderGraph::CheckBoundBuffers(castl::string& outParameter,
		castl::string& outReason) const
	{
		for (auto const& record : m_PassParams)
		{
			if (record.parameters == nullptr)
			{
				continue;
			}
			ShaderCompilerSlang::ShaderStructData const* data = record.parameters->GetDataForTest();
			if (data == nullptr)
			{
				continue;
			}
			for (auto const& buffer : data->m_Buffers)
			{
				const castl::string passText = castl::string("pass ") +
					std::to_string(record.passIndex).c_str() + " 的块 " + record.blockName.c_str();
				const cacore::NameHash bufferName = record.parameters->GetBoundBufferName(buffer.m_Name);
				if (!record.parameters->HasBoundBuffer(buffer.m_Name))
				{
					outParameter = "field";
					outReason = passText + " 没有为缓冲字段 " + buffer.m_Name.c_str() + " 调 SetBuffer";
					return 1;
				}
				if (m_Buffers.find(bufferName) == m_Buffers.end())
				{
					outParameter = "bufferName";
					outReason = passText + " 的字段 " + buffer.m_Name.c_str() +
						" 引用了本图未登记的缓冲：" +
						(bufferName.Valid() ? bufferName.c_str() : "(空)");
					return 1;
				}
			}
		}
		return 0;
	}

	// csharp-render-texture-sampling 任务 2.2：纹理字段的只读核对口。返回图侧登记表里那个参数
	// 结构持有的引用本身（对象同一性判据），没有任何计数能替代"解析到哪个对象"。
	castl::shared_ptr<IScriptTexture> CScriptRenderGraph::GetBoundImageForTest(uint32_t passIndex,
		cacore::NameHash const& blockName, cacore::NameHash const& field) const
	{
		for (auto it = m_PassParams.rbegin(); it != m_PassParams.rend(); ++it)
		{
			if (it->passIndex == passIndex && it->blockName == blockName)
			{
				return it->parameters == nullptr ? nullptr : it->parameters->GetBoundImage(field);
			}
		}
		return nullptr;
	}

	int32_t CScriptRenderGraph::CheckBoundTextures(castl::string& outParameter,
		castl::string& outReason) const
	{
		for (auto const& record : m_PassParams)
		{
			if (record.parameters == nullptr)
			{
				continue;
			}
			ShaderCompilerSlang::ShaderStructData const* data = record.parameters->GetDataForTest();
			if (data == nullptr)
			{
				continue;
			}
			const castl::string passText = castl::string("pass ") +
				std::to_string(record.passIndex).c_str() + " 的块 " + record.blockName.c_str();
			for (auto const& texture : data->m_Textures)
			{
				if (!record.parameters->HasBoundImage(texture.m_Name))
				{
					outParameter = "field";
					outReason = passText + " 没有为纹理字段 " + texture.m_Name.c_str() +
						" 调 SetImage";
					return 1;
				}
			}
			for (auto const& sampler : data->m_TextureSamplers)
			{
				if (!record.parameters->HasBoundSampler(sampler))
				{
					outParameter = "field";
					outReason = passText + " 没有为采样器字段 " + sampler.c_str() +
						" 调 SetSampler";
					return 1;
				}
			}
		}
		return 0;
	}

	// ---- CScriptRenderBackend ----

	CScriptRenderBackend::CScriptRenderBackend(CRenderBackend* backend,
		thread_management::CThreadManager* threadManager)
		: m_Backend(backend)
		, m_ThreadManager(threadManager)
	{
	}

	int32_t CScriptRenderBackend::Fail(char const* parameter, char const* reason)
	{
		m_LastError = castl::string("IScriptRenderBackend.") + (parameter == nullptr ? "?" : parameter) +
			"：" + (reason == nullptr ? "参数无效" : reason);
		return 1;
	}

	castl::shared_ptr<IScriptRenderTarget> CScriptRenderBackend::GetWindowTarget(
		castl::shared_ptr<cawindow::IWindow> const& window)
	{
		if (window == nullptr)
		{
			Fail("window", "窗口不能为空");
			return nullptr;
		}
		if (m_Backend == nullptr)
		{
			Fail("window", "渲染后端不可用");
			return nullptr;
		}
		castl::shared_ptr<WindowHandle> handle = m_Backend->GetWindowHandle(window);
		if (handle == nullptr)
		{
			Fail("window", "后端没有返回窗口句柄");
			return nullptr;
		}
		GPUTextureDescriptor const descriptor = handle->GetBackbufferDescriptor();
		if (descriptor.width == 0 || descriptor.height == 0)
		{
			Fail("window", "窗口后台缓冲尺寸为零");
			return nullptr;
		}
		ClearError();
		auto target = castl::make_shared<CScriptRenderTarget>(ImageHandle(handle), descriptor, true);
		m_LastWindowTarget = target;
		return target;
	}

	castl::shared_ptr<IScriptRenderTarget> CScriptRenderBackend::CreateCaptureTarget(uint32_t width,
		uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			Fail("width", "离屏目标尺寸必须非零");
			return nullptr;
		}
		if (m_Backend == nullptr)
		{
			Fail("width", "渲染后端不可用");
			return nullptr;
		}
		// 固定使用当前原生验证路径的 RGBA8、二维、单层、单 mip、单采样。
		GPUTextureDescriptor const descriptor = GPUTextureDescriptor::Create(width, height,
			ETextureFormat::E_R8G8B8A8_UNORM, ETextureType::e2D, 1u, 1u, EMultiSampleCount::e1);
		castl::shared_ptr<GPUTexture> texture = m_Backend->CreateGPUTexture(descriptor,
			ETextureAccessType::eTransferSrc | ETextureAccessType::eTransferDst | ETextureAccessType::eRT);
		if (texture == nullptr)
		{
			Fail("width", "后端拒绝了离屏纹理创建");
			return nullptr;
		}
		ClearError();
		auto target = castl::make_shared<CScriptRenderTarget>(ImageHandle(texture), descriptor, false);
		m_LastCaptureTarget = target;
		++m_CreateCaptureTargetCount;
		return target;
	}

	castl::shared_ptr<IScriptRenderGraph> CScriptRenderBackend::CreateGraph()
	{
		ClearError();
		// 图需要后端引用：`SetPassParam` 要按 pass 的着色器路径查根结构元数据。
		return castl::make_shared<CScriptRenderGraph>(m_Backend, &m_ShaderCounters);
	}

	// csharp-render-texture-sampling 任务 2.1：一次调用完成「创建 → 图内上传 → 转采样态 → 同步等待」。
	// 拒绝清单与 `CreateShaderStruct` 同形：空句柄 + `GetLastError()` 点名，且 MUST NOT 进入后端
	// `CreateGPUTexture` 的非法参数路径（尺寸 / 字节数 / 重名都在前面挡下）。
	castl::shared_ptr<IScriptTexture> CScriptRenderBackend::CreateTexture(cacore::NameHash const& name,
		uint32_t width, uint32_t height, castl::vector<uint8_t> const& rgba)
	{
		if (!name.Valid())
		{
			Fail("name", "纹理名称不能为空");
			return nullptr;
		}
		if (width == 0u || height == 0u)
		{
			Fail("width", "纹理尺寸必须非零");
			return nullptr;
		}
		if (m_Backend == nullptr || m_ThreadManager == nullptr)
		{
			Fail("name", "渲染后端或线程管理器不可用");
			return nullptr;
		}
		// 字节数只认 `GetImageByteSize`（64 位公开来源）：32 位的 `width * height * 4` 在
		// `65536 × 16384` 上回绕成 0，会骗过任何按 32 位自算的校验。
		const uint64_t expectedBytes =
			GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM);
		const uint64_t receivedBytes = static_cast<uint64_t>(rgba.size());
		if (expectedBytes != receivedBytes)
		{
			Fail("rgba", (castl::string("字节数与 GetImageByteSize 不一致：期望 ") +
				std::to_string(expectedBytes).c_str() + " 字节（RGBA8，宽 " +
				std::to_string(width).c_str() + " × 高 " + std::to_string(height).c_str() +
				"），实际收到 " + std::to_string(receivedBytes).c_str() + " 字节").c_str());
			return nullptr;
		}
		if (m_Textures.find(name) != m_Textures.end())
		{
			Fail("name", (castl::string("同名纹理已经创建过：") + name.c_str()).c_str());
			return nullptr;
		}
		GPUTextureDescriptor const descriptor =
			GPUTextureDescriptor::Create(width, height, ETextureFormat::E_R8G8B8A8_UNORM);
		castl::shared_ptr<GPUTexture> nativeTexture = m_Backend->CreateGPUTexture(descriptor,
			ETextureAccessType::eTransferDst | ETextureAccessType::eSampled);
		if (nativeTexture == nullptr)
		{
			Fail("name", (castl::string("后端没有创建出纹理：") + name.c_str()).c_str());
			return nullptr;
		}
		// 内部上传图：不建窄对象、不进图登记表、不含任何渲染 pass（原生测试上传段的同型先例）。
		castl::shared_ptr<GPUGraph> uploadGraph = castl::make_shared<GPUGraph>();
		const ImageHandle imageHandle(nativeTexture);
		uploadGraph->ScheduleData(imageHandle, rgba.data(), static_cast<uint64_t>(rgba.size()));
		uploadGraph->Finalize(imageHandle, ETextureAccessType::eSampled);
		// 后端直连：不经适配器的 `SubmitGraph` / `WaitIdle`（D24）—— 那会抬高提交执行/等待计数，
		// 并提前清掉 `m_Pending`。上传执行单列计数。
		castl::shared_ptr<thread_management::TaskScheduler> scheduler = m_ThreadManager->NewScheduler();
		if (scheduler == nullptr)
		{
			Fail("name", "线程管理器没有创建出调度器");
			return nullptr;
		}
		++m_TextureUploadExecuteCount;
		m_Backend->ExecuteGraph(scheduler.get(), uploadGraph);
		m_Backend->WaitIdle();

		castl::shared_ptr<IScriptTexture> texture =
			castl::make_shared<CScriptTexture>(width, height, nativeTexture);
		m_Textures[name] = nativeTexture;
		m_LastCreatedTexture = texture;
		++m_ShaderCounters.createTexture;
		ClearError();
		return texture;
	}

	castl::shared_ptr<IScriptShaderStruct> CScriptRenderBackend::CreateShaderStruct(
		cacore::NameHash const& structTypeName)
	{
		if (!structTypeName.Valid())
		{
			Fail("structTypeName", "参数结构类型名不能为空");
			return nullptr;
		}
		if (m_Backend == nullptr)
		{
			Fail("structTypeName", "渲染后端不可用");
			return nullptr;
		}
		// 先查编译元数据：未知类型必须在这里被挡下。改前直接把类型名交给
		// `CRenderBackend::CreateShaderStruct`，D3D12 侧会拿空元数据继续构造并在构造函数体里解引用。
		ShaderCompilerSlang::ShaderStructData const* data = m_Backend->FindShaderStructData(structTypeName);
		if (data == nullptr)
		{
			Fail("structTypeName", (castl::string("类型名没有编译元数据（不是任何着色器结构）：") +
				structTypeName.c_str()).c_str());
			return nullptr;
		}
		castl::shared_ptr<ShaderStruct> shaderStruct = m_Backend->CreateShaderStruct(structTypeName);
		if (shaderStruct == nullptr)
		{
			Fail("structTypeName", (castl::string("后端没有创建出参数结构：") +
				structTypeName.c_str()).c_str());
			return nullptr;
		}
		++m_ShaderCounters.createShaderStruct;
		ClearError();
		return castl::make_shared<CScriptShaderStruct>(structTypeName, data, shaderStruct,
			&m_ShaderCounters);
	}

	int32_t CScriptRenderBackend::SubmitGraph(castl::shared_ptr<IScriptRenderGraph> const& graph)
	{
		if (graph == nullptr)
		{
			return Fail("graph", "提交的图不能为空");
		}
		auto* concrete = dynamic_cast<CScriptRenderGraph*>(graph.get());
		if (concrete == nullptr)
		{
			return Fail("graph", "图不是本适配面创建的对象");
		}
		// 测试钩子：先记下这次提交的图（弱引用）。空图会在下面被拒绝，但 `skip-draw` 的负向判据
		// 要在拒绝之前读到它的 pass 数 = 0（与正常模式的 2 个 pass 对比）。
		m_LastGraph = std::static_pointer_cast<CScriptRenderGraph>(graph);
		castl::shared_ptr<GPUGraph> const& gpuGraph = concrete->GetGraphForTest();
		if (gpuGraph == nullptr || concrete->GetPassCount() == 0)
		{
			return Fail("graph", "空图不能提交（至少需要一个 render pass）");
		}
		if (m_Backend == nullptr || m_ThreadManager == nullptr)
		{
			return Fail("graph", "渲染后端或线程管理器不可用");
		}
		// 必需参数块检查（csharp-render-shader-constants 任务 2.2）：在调用 `ExecuteGraph` **之前**
		// 按每个 pass 的着色器根结构核对 `m_SubStructReferences` 列出的块是否都已绑定。
		// 两后端在空参数表下执行会触发 `CA_ASSERT_BREAK` 断点 —— 判据不能依赖执行期读默认值。
		// 根结构未登记的着色器（既有三角形 / 窗口路径）必需集合为空，行为不变。
		for (uint32_t passIndex = 0; passIndex < concrete->GetPassCount(); ++passIndex)
		{
			auto const& pass = gpuGraph->GetRenderPasses()[passIndex];
			cacore::PathHash const shaderPath = pass.GetPipelineStates().m_ShaderInfo.path;
			ShaderCompilerSlang::ShaderStructData const* rootStruct =
				m_Backend->FindShaderRootStructData(shaderPath);
			if (rootStruct == nullptr)
			{
				continue;
			}
			for (auto const& reference : rootStruct->m_SubStructReferences)
			{
				auto bound = pass.GetShaderStructs().find(reference.m_Name);
				if (bound == pass.GetShaderStructs().end() || bound->second == nullptr)
				{
					return Fail("blockName", (castl::string("pass ") +
						std::to_string(passIndex).c_str() + " 的着色器 " + shaderPath.c_str() +
						" 缺少必需参数块：" + reference.m_Name.c_str()).c_str());
				}
			}
		}
		// csharp-render-structured-buffer 任务 2.2：结构化缓冲引用检查必须在进入执行之前完成 ——
		// 字段没写过、或缓冲名不在**本图**登记表里都点名拒绝，`ExecuteGraph` 调用次数保持 0。
		{
			castl::string parameter;
			castl::string reason;
			if (concrete->CheckBoundBuffers(parameter, reason) != 0)
			{
				return Fail(parameter.c_str(), reason.c_str());
			}
		}
		// csharp-render-texture-sampling 任务 2.2：纹理与采样器必需性检查，同样在进入执行之前。
		{
			castl::string parameter;
			castl::string reason;
			if (concrete->CheckBoundTextures(parameter, reason) != 0)
			{
				return Fail(parameter.c_str(), reason.c_str());
			}
		}
		// 待完成集合先持有强引用，再交给后端执行：脚本随后释放自己的表面对象也不影响执行。
		m_Pending.push_back(graph);
		m_LastSubmittedGraph = gpuGraph;
		castl::shared_ptr<thread_management::TaskScheduler> scheduler = m_ThreadManager->NewScheduler();
		if (scheduler == nullptr)
		{
			m_Pending.pop_back();
			return Fail("graph", "线程管理器没有创建出调度器");
		}
		++m_ExecuteGraphCount;
		m_Backend->ExecuteGraph(scheduler.get(), gpuGraph);
		ClearError();
		return 0;
	}

	void CScriptRenderBackend::WaitIdle()
	{
		if (m_Backend != nullptr)
		{
			m_Backend->WaitIdle();
		}
		++m_WaitIdleCount;
		m_Pending.clear();
		ClearError();
	}

	castl::string CScriptRenderBackend::GetLastError() const
	{
		return m_LastError;
	}
}
