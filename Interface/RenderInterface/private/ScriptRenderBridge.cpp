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
