// 脚本窄渲染适配面的原生实现（csharp-render-backend-triangle 任务 2.1 / 2.2）。
//
// 所有拒绝都发生在进入具体后端之前：参数名原样写进 `GetLastError()`，调用方据此定位。
// 成功调用先清空旧错误 —— 否则"上一次失败"会一直挂在这个对象上，判据分不清哪次调用出的错。
#include "ScriptRenderBridge.h"

#include <CRenderBackend.h>
#include <ThreadManager.h>

#include <utility>

namespace graphics_backend
{
	namespace
	{
		// POSITION / COLOR 两个 `float3` 语义各占 12 字节（design 二）。
		constexpr uint32_t kFloat3ByteSize = 12u;
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

	// ---- CScriptRenderGraph ----

	CScriptRenderGraph::CScriptRenderGraph()
		: m_Graph(castl::make_shared<GPUGraph>())
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
		return target;
	}

	castl::shared_ptr<IScriptRenderGraph> CScriptRenderBackend::CreateGraph()
	{
		ClearError();
		return castl::make_shared<CScriptRenderGraph>();
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
		// 待完成集合先持有强引用，再交给后端执行：脚本随后释放自己的表面对象也不影响执行。
		m_Pending.push_back(graph);
		m_LastSubmittedGraph = gpuGraph;
		castl::shared_ptr<thread_management::TaskScheduler> scheduler = m_ThreadManager->NewScheduler();
		if (scheduler == nullptr)
		{
			m_Pending.pop_back();
			return Fail("graph", "线程管理器没有创建出调度器");
		}
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
