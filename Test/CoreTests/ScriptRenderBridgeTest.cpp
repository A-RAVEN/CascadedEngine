// 脚本窄渲染适配面的原生判据（csharp-render-backend-triangle 任务 1.1 / 2.1 / 2.2）。
//
// 一半判据直接对着原生实现：真实 `GPUGraph` 的上传字节、两个顶点属性、绘制数、清除色、
// 呈现清单、等待次数与待完成集合；另一半走生成的 C ABI 入口，证明错误类型 / 已释放句柄
// 在进入实现之前就被挡下（实现调用次数不涨）。
#include "../../Interface/RenderInterface/private/ScriptRenderBridge.h"

#include <CRenderBackend.h>
#include <ThreadManager.h>

#include "IScriptRenderTarget.Bindings.cpp"
#include "IScriptRenderGraph.Bindings.cpp"
#include "IScriptRenderBackend.Bindings.cpp"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace
{
	bool Expect(bool condition, char const* what)
	{
		if (!condition)
		{
			std::printf("  [脚本渲染适配] 失败：%s\n", what);
		}
		return condition;
	}

	// ---- 假线程管理器：只为让 `SubmitGraph` 走真实的 NewScheduler 分支 ----
	class FakeTaskScheduler final : public thread_management::TaskScheduler
	{
	public:
		thread_management::CTask* NewTask() override { return nullptr; }
		thread_management::TaskParallelFor* NewTaskParallelFor() override { return nullptr; }
		thread_management::CTaskGraph* NewTaskGraph() override { return nullptr; }
		void Execute(castl::array_ref<thread_management::TaskBase*>, bool) override {}
		void WaitAll() override {}
	};

	class FakeThreadManager final : public thread_management::CThreadManager
	{
	public:
		void InitializeThreadCount(catimer::TimerSystem*, uint32_t) override {}
		void AddTaskQueue(cacore::NameHash const&, castl::array_ref<uint32_t>) override {}
		void LoopFunction(castl::function<void(thread_management::TaskScheduler*)>,
			cacore::NameHash const&) override {}
		castl::shared_ptr<thread_management::TaskScheduler> NewScheduler() override
		{
			++schedulerCalls;
			return castl::make_shared<FakeTaskScheduler>();
		}
		void Run() override {}
		void LogStatus() const override {}

		int schedulerCalls = 0;
	};

	// ---- 假 GPU 后端：只记录"实现被进入"的次数，不产生真实 GPU 工作 ----
	class FakeRenderBackend final : public graphics_backend::CRenderBackend
	{
	public:
		void ExecuteGraph(thread_management::TaskScheduler*,
			castl::shared_ptr<graphics_backend::GPUGraph> const& graph) override
		{
			++executeGraphCalls;
			lastGraph = graph;
		}
		void Release() override {}
		castl::shared_ptr<graphics_backend::GPUBuffer> CreateGPUBuffer(
			graphics_backend::GPUBufferDescriptor const&, EBufferUsageFlags) override
		{
			return nullptr;
		}
		castl::shared_ptr<graphics_backend::GPUTexture> CreateGPUTexture(
			graphics_backend::GPUTextureDescriptor const& descriptor,
			ETextureAccessTypeFlags) override
		{
			++createTextureCalls;
			auto texture = castl::make_shared<FakeTexture>();
			texture->descriptor = descriptor;
			return texture;
		}
		castl::shared_ptr<graphics_backend::ShaderStruct> CreateShaderStruct(
			cacore::NameHash const&) override
		{
			return nullptr;
		}
		castl::shared_ptr<graphics_backend::WindowHandle> GetWindowHandle(
			castl::shared_ptr<cawindow::IWindow>) override
		{
			++getWindowHandleCalls;
			return nullptr;
		}
		bool AnyWindowRunning() override { return false; }
		void WaitIdle() override { ++waitIdleCalls; }
		std::unique_ptr<graphics_backend::IReadbackToken> Readback(
			graphics_backend::ImageHandle const&, std::span<uint8_t>) override
		{
			return nullptr;
		}
		std::unique_ptr<graphics_backend::IReadbackToken> Readback(
			graphics_backend::BufferHandle const&, std::span<uint8_t>) override
		{
			return nullptr;
		}

		class FakeTexture final : public graphics_backend::GPUTexture
		{
		public:
			graphics_backend::GPUTextureDescriptor const& GetDescriptor() const override
			{
				return descriptor;
			}
			void SetName(castl::string const& value) override { name = value; }
			castl::string const& GetName() const override { return name; }

			graphics_backend::GPUTextureDescriptor descriptor{};
			castl::string name;
		};

		int executeGraphCalls = 0;
		int waitIdleCalls = 0;
		int createTextureCalls = 0;
		int getWindowHandleCalls = 0;
		castl::weak_ptr<graphics_backend::GPUGraph> lastGraph;
	};

	class FakeWindowHandle final : public graphics_backend::WindowHandle
	{
	public:
		graphics_backend::uint2 GetSizeSafe() const override
		{
			return graphics_backend::uint2{ descriptor.width, descriptor.height };
		}
		graphics_backend::GPUTextureDescriptor const& GetBackbufferDescriptor() const override
		{
			return descriptor;
		}

		graphics_backend::GPUTextureDescriptor descriptor =
			graphics_backend::GPUTextureDescriptor::Create(64u, 48u,
				ETextureFormat::E_R8G8B8A8_UNORM);
	};

	// 非本适配面创建的图：提交必须在进入后端之前拒绝。
	class ForeignGraph final : public graphics_backend::IScriptRenderGraph
	{
	public:
		int32_t AddBuffer(cacore::NameHash const&, graphics_backend::GPUBufferDescriptor const&,
			castl::vector<uint8_t> const&) override
		{
			return 0;
		}
		int32_t SetPositionColorStream(cacore::NameHash const&, uint32_t, uint32_t, uint32_t) override
		{
			return 0;
		}
		int32_t AddTrianglePass(castl::shared_ptr<graphics_backend::IScriptRenderTarget> const&,
			cacore::PathHash const&, cacore::NameHash const&, cacore::NameHash const&, uint32_t, float,
			float, float, float) override
		{
			return 0;
		}
		int32_t Present(castl::shared_ptr<graphics_backend::IScriptRenderTarget> const&) override
		{
			return 0;
		}
		uint32_t GetPassCount() const override { return 1u; }
		castl::string GetLastError() const override { return castl::string(); }
	};

	castl::string ReadGraphError(void* self)
	{
		const uint64_t length = IScriptRenderGraph_GetLastErrorLen(self);
		if (length == 0u)
		{
			return castl::string();
		}
		std::vector<char> buffer(static_cast<std::size_t>(length) + 1u, '\0');
		IScriptRenderGraph_CopyGetLastError(self, buffer.data(), static_cast<uint64_t>(buffer.size()));
		return castl::string(buffer.data());
	}

	bool Contains(castl::string const& text, char const* token)
	{
		return text.find(token) != castl::string::npos;
	}

	// 一次拒绝的三条共同判据：非零状态、错误文本点名参数、图/后端没有被推进。
	bool ExpectReject(graphics_backend::CScriptRenderGraph& graph, int32_t status,
		char const* token, uint32_t expectedPassCount, FakeRenderBackend const& backend)
	{
		const bool statusOk = Expect(status != 0, "拒绝路径没有返回非零状态");
		const castl::string text = graph.GetLastError();
		const bool textOk = Expect(Contains(text, token),
			(std::string("错误文本没有点名参数 ") + token + "，实际：" +
				std::string(text.c_str())).c_str());
		const bool passOk = Expect(graph.GetPassCount() == expectedPassCount,
			"拒绝路径仍然向图里加了 pass");
		const bool backendOk = Expect(backend.executeGraphCalls == 0,
			"拒绝路径进入了具体后端执行");
		std::printf("  [拒绝] 参数=%-14s status=%d 点名=%d 文本=%s pass=%u execCalls=%d\n", token, status,
			textOk ? 1 : 0, text.c_str(), graph.GetPassCount(), backend.executeGraphCalls);
		return statusOk && textOk && passOk && backendOk;
	}
}

bool RunScriptRenderBridgeTest()
{
	FakeRenderBackend backend;
	FakeThreadManager threads;
	graphics_backend::CScriptRenderBackend adapter(&backend, &threads);
	bool ok = true;

	// ---- ① 后端创建离屏目标：固定格式、尺寸快照 ----
	auto capture = adapter.CreateCaptureTarget(64u, 48u);
	ok = Expect(capture != nullptr, "CreateCaptureTarget 返回空") && ok;
	auto* captureTarget = dynamic_cast<graphics_backend::CScriptRenderTarget*>(capture.get());
	ok = Expect(captureTarget != nullptr, "CreateCaptureTarget 返回的不是本适配面的目标") && ok;
	ok = Expect(backend.createTextureCalls == 1, "CreateCaptureTarget 没有调用后端的纹理创建") && ok;
	ok = Expect(captureTarget->GetWidth() == 64u && captureTarget->GetHeight() == 48u,
		"离屏目标的尺寸快照不对") && ok;
	ok = Expect(captureTarget->GetDescriptorForTest().format ==
		ETextureFormat::E_R8G8B8A8_UNORM,
		"离屏目标不是 RGBA8") && ok;
	ok = Expect(!captureTarget->IsBackbuffer(), "离屏目标被标成了后台缓冲") && ok;
	ok = Expect(adapter.CreateCaptureTarget(0u, 48u) == nullptr &&
		Contains(adapter.GetLastError(), "width"),
		"零宽离屏目标没有被拒绝") && ok;

	// ---- ② 建图：缓冲、字节复制、属性、绘制、清除色、呈现清单 ----
	auto graph = adapter.CreateGraph();
	ok = Expect(graph != nullptr, "CreateGraph 返回空") && ok;
	auto* scriptGraph = dynamic_cast<graphics_backend::CScriptRenderGraph*>(graph.get());
	ok = Expect(scriptGraph != nullptr, "CreateGraph 返回的不是本适配面的图") && ok;

	std::vector<uint8_t> vertices(72u);
	for (std::size_t index = 0; index < vertices.size(); ++index)
	{
		vertices[index] = static_cast<uint8_t>(index + 1u);
	}
	const std::vector<uint8_t> expectedBytes = vertices;
	const int32_t addStatus = graph->AddBuffer(CANAME("TestVerticesInput"),
		graphics_backend::GPUBufferDescriptor::Create(3u, 24u), vertices);
	ok = Expect(addStatus == 0, "AddBuffer 合法调用被拒绝") && ok;
	// 同步复制：调用返回后改输入数组，原生持有区必须逐字节不变。
	for (auto& byte : vertices)
	{
		byte = 0xA5u;
	}
	auto const& upload = scriptGraph->GetGraphForTest()->GetUploadDataHolder().m_Data;
	ok = Expect(upload.size() == expectedBytes.size(),
		"上传持有区的字节数不对") && ok;
	bool bytesMatch = upload.size() == expectedBytes.size();
	for (std::size_t index = 0; bytesMatch && index < expectedBytes.size(); ++index)
	{
		bytesMatch = upload[index] == expectedBytes[index];
	}
	ok = Expect(bytesMatch, "AddBuffer 之后修改输入数组改变了原生字节") && ok;
	std::printf("  [字节复制] 上传区 %zu 字节 / 输入 %zu 字节 / 改输入后逐字节一致=%d\n", upload.size(),
		expectedBytes.size(), bytesMatch ? 1 : 0);
	auto const* bufferDescriptor = scriptGraph->GetGraphForTest()->GetBufferManager().DescriptorIDToDescriptor(
		scriptGraph->GetGraphForTest()->GetBufferManager().GetDescriptorIndex(
			graphics_backend::BufferHandle(CANAME("TestVerticesInput")).GetKey()));
	ok = Expect(bufferDescriptor != nullptr && bufferDescriptor->count == 3u &&
		bufferDescriptor->stride == 24u,
		"图里没有对应的缓冲描述") && ok;

	ok = Expect(graph->SetPositionColorStream(CANAME("TestVerticesInput"), 24u, 0u, 12u) == 0,
		"SetPositionColorStream 合法调用被拒绝") && ok;
	ok = Expect(graph->AddTrianglePass(capture, CAPATH("Shaders/Test/TestSimpleTriangle"),
		CANAME("TestVerticesInput"), CANAME("TestVerticesInput"), 3u, 0.0f, 1.0f, 0.0f, 1.0f) == 0,
		"AddTrianglePass 合法调用被拒绝") && ok;
	ok = Expect(graph->GetPassCount() == 1u, "合法的 AddTrianglePass 没有加出一个 pass") && ok;

	auto const& passes = scriptGraph->GetGraphForTest()->GetRenderPasses();
	ok = Expect(passes.size() == 1u, "真实 GPUGraph 里的 pass 数不对") && ok;
	if (passes.size() == 1u)
	{
		auto const& pass = passes.front();
		ok = Expect(pass.GetAttachments().size() == 1u &&
			pass.GetAttachments()[0] == captureTarget->GetImageForTest(),
			"pass 的渲染目标不是 C# 给的那个离屏目标") && ok;
		auto const& clear = pass.GetAttachmentConfig(0).clearValue.color;
		ok = Expect(clear.r == 0.0f && clear.g == 1.0f && clear.b == 0.0f && clear.a == 1.0f,
			"pass 的清除色不是调用方给的值") && ok;
		ok = Expect(pass.GetPipelineStates().m_ShaderInfo.path ==
			CAPATH("Shaders/Test/TestSimpleTriangle"),
			"pass 的着色器路径不对") && ok;
		auto const& batches = pass.GetDrawCallBatches();
		ok = Expect(batches.size() == 1u, "pass 里的绘制批次数不对") && ok;
		if (batches.size() == 1u)
		{
			auto const& batch = batches.front();
			auto stream = batch.m_VertexInputDescs.find(CANAME("TestVerticesInput"));
			ok = Expect(stream != batch.m_VertexInputDescs.end(),
				"绘制批次里没有登记的顶点流") && ok;
			if (stream != batch.m_VertexInputDescs.end())
			{
				auto const& descriptor = stream->second.Get();
				ok = Expect(descriptor.stride == 24u && descriptor.attributes.size() == 2u,
					"顶点流不是 24 字节步长 + 两个属性") && ok;
				if (descriptor.attributes.size() == 2u)
				{
					ok = Expect(descriptor.attributes[0].offset == 0u &&
						descriptor.attributes[0].format ==
							VertexInputFormat::eR32G32B32_SFloat &&
						descriptor.attributes[0].semanticName == CANAME("POSITION"),
						"POSITION 属性不是 offset 0 的 float3") && ok;
					ok = Expect(descriptor.attributes[1].offset == 12u &&
						descriptor.attributes[1].format ==
							VertexInputFormat::eR32G32B32_SFloat &&
						descriptor.attributes[1].semanticName == CANAME("COLOR"),
						"COLOR 属性不是 offset 12 的 float3") && ok;
				}
			}
			ok = Expect(batch.m_DrawCalls.size() == 1u &&
				batch.m_DrawCalls[0].GetDrawInfo().vertexCount == 3u,
				"绘制数不是调用方给的 3") && ok;
			auto const& bound = batch.m_DrawCalls[0].GetVertexBuffers();
			ok = Expect(bound.find(CANAME("TestVerticesInput")) != bound.end(),
				"绘制没有绑定调用方给的缓冲") && ok;
		}
	}

	// ---- ③ 拒绝路径：每一项都必须在进入后端之前失败并点名参数 ----
	const uint32_t passCountBeforeRejects = graph->GetPassCount();
	ok = ExpectReject(*scriptGraph,
		graph->AddBuffer(CANAME("ShortBuffer"),
			graphics_backend::GPUBufferDescriptor::Create(3u, 24u),
			std::vector<uint8_t>(71u)),
		"data", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddBuffer(CANAME("TestVerticesInput"),
			graphics_backend::GPUBufferDescriptor::Create(3u, 24u), expectedBytes),
		"name", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->SetPositionColorStream(CANAME("BadStream"), 20u, 12u, 0u),
		"positionOffset", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->SetPositionColorStream(CANAME("BadStream"), 20u, 0u, 12u),
		"colorOffset", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddTrianglePass(capture, CAPATH("Shaders/Test/TestSimpleTriangle"),
			CANAME("UnknownStream"), CANAME("TestVerticesInput"), 3u, 0.0f, 0.0f, 0.0f, 1.0f),
		"streamName", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddTrianglePass(capture, CAPATH("Shaders/Test/TestSimpleTriangle"),
			CANAME("TestVerticesInput"), CANAME("UnknownBuffer"), 3u, 0.0f, 0.0f, 0.0f, 1.0f),
		"bufferName", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddTrianglePass(capture, CAPATH("Shaders/Test/TestSimpleTriangle"),
			CANAME("TestVerticesInput"), CANAME("TestVerticesInput"), 0u, 0.0f, 0.0f, 0.0f, 1.0f),
		"vertexCount", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddTrianglePass(capture, CAPATH("Shaders/Test/TestSimpleTriangle"),
			CANAME("TestVerticesInput"), CANAME("TestVerticesInput"), 4u, 0.0f, 0.0f, 0.0f, 1.0f),
		"vertexCount", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph,
		graph->AddTrianglePass(nullptr, CAPATH("Shaders/Test/TestSimpleTriangle"),
			CANAME("TestVerticesInput"), CANAME("TestVerticesInput"), 3u, 0.0f, 0.0f, 0.0f, 1.0f),
		"target", passCountBeforeRejects, backend) && ok;
	ok = ExpectReject(*scriptGraph, graph->Present(capture), "target", passCountBeforeRejects,
		backend) && ok;
	ok = ExpectReject(*scriptGraph, graph->Present(nullptr), "target", passCountBeforeRejects,
		backend) && ok;
	ok = ExpectReject(*scriptGraph, graph->Present(nullptr), "target", passCountBeforeRejects,
		backend) && ok;

	// 一次成功调用必须清空旧错误（错误文本不是"粘住"的）。
	ok = Expect(graph->Present(capture) != 0 && !graph->GetLastError().empty(),
		"错误目标呈现没有留下错误文本") && ok;
	auto windowHandle = castl::make_shared<FakeWindowHandle>();
	auto windowTarget = castl::make_shared<graphics_backend::CScriptRenderTarget>(
		graphics_backend::ImageHandle(castl::shared_ptr<graphics_backend::WindowHandle>(windowHandle)),
		windowHandle->descriptor, true);
	ok = Expect(graph->Present(windowTarget) == 0, "Present 后台缓冲目标被拒绝") && ok;
	ok = Expect(graph->GetLastError().empty(), "成功调用没有清空旧错误") && ok;
	ok = Expect(scriptGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers.size() == 1u,
		"真实 GPUGraph 的呈现清单里没有窗口后台缓冲") && ok;
	ok = Expect(scriptGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers[0] ==
		windowTarget->GetImageForTest(),
		"呈现清单里的不是调用方给的那个后台缓冲") && ok;
	ok = Expect(backend.executeGraphCalls == 0, "建图阶段就进入了具体后端") && ok;
	{
		auto const& firstPass = scriptGraph->GetGraphForTest()->GetRenderPasses().front();
		auto const& firstBatch = firstPass.GetDrawCallBatches().front();
		auto const& streamDesc = firstBatch.m_VertexInputDescs.begin()->second.Get();
		auto const& clear = firstPass.GetAttachmentConfig(0).clearValue.color;
		std::printf("  [图结构] pass=%zu 属性=%zu 步长=%u 绘制=%u 清除色=(%.1f,%.1f,%.1f,%.1f) 呈现=%zu\n",
			scriptGraph->GetGraphForTest()->GetRenderPasses().size(), streamDesc.attributes.size(),
			streamDesc.stride, firstBatch.m_DrawCalls[0].GetDrawInfo().vertexCount, clear.r, clear.g,
			clear.b, clear.a, scriptGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers.size());
	}
	ok = Expect(graph->GetLastError().empty(), "成功调用没有清空旧错误") && ok;
	std::printf("  [错误通道] 成功调用后 GetLastError 长度=%zu\n", graph->GetLastError().size());

	// ---- ④ 提交与等待：待完成集合持强引用，WaitIdle 之后清理 ----
	ok = Expect(adapter.SubmitGraph(nullptr) != 0 && Contains(adapter.GetLastError(), "graph") &&
		backend.executeGraphCalls == 0,
		"空图指针提交没有被拒绝") && ok;
	castl::shared_ptr<graphics_backend::IScriptRenderGraph> foreign =
		castl::make_shared<ForeignGraph>();
	ok = Expect(adapter.SubmitGraph(foreign) != 0 && Contains(adapter.GetLastError(), "graph") &&
		backend.executeGraphCalls == 0,
		"外部图实现没有被拒绝") && ok;

	ok = Expect(adapter.SubmitGraph(graph) == 0, "合法图提交失败") && ok;
	ok = Expect(backend.executeGraphCalls == 1 && threads.schedulerCalls == 1,
		"提交没有走 NewScheduler + ExecuteGraph") && ok;
	ok = Expect(adapter.GetPendingGraphCountForTest() == 1u,
		"待完成集合没有持有图强引用") && ok;
	ok = Expect(adapter.GetWaitIdleCountForTest() == 0u, "提交之后等待计数已经开始涨") && ok;
	ok = Expect(!adapter.GetLastSubmittedGraphForTest().expired(),
		"提交之后最近一次图的弱引用立即失效") && ok;

	castl::shared_ptr<graphics_backend::GPUGraph> submittedGraph =
		adapter.GetLastSubmittedGraphForTest().lock();
	graph.reset(); // 脚本释放自己的图表面对象：待完成集合必须还持有强引用
	ok = Expect(!adapter.GetLastSubmittedGraphForTest().expired(),
		"释放脚本图表面对象之后原生图也跟着没了") && ok;
	ok = Expect(adapter.GetPendingGraphCountForTest() == 1u,
		"释放脚本图表面对象之后待完成集合被提前清空") && ok;

	adapter.WaitIdle();
	ok = Expect(adapter.GetWaitIdleCountForTest() == 1u && backend.waitIdleCalls == 1,
		"WaitIdle 没有真正等后端") && ok;
	ok = Expect(adapter.GetPendingGraphCountForTest() == 0u,
		"WaitIdle 之后待完成集合没有清理") && ok;
	std::printf("  [提交生命周期] ExecuteGraph=%d NewScheduler=%d WaitIdle=%u 后端WaitIdle=%d 待完成=%u\n",
		backend.executeGraphCalls, threads.schedulerCalls, adapter.GetWaitIdleCountForTest(),
		backend.waitIdleCalls, adapter.GetPendingGraphCountForTest());
	submittedGraph.reset();
	ok = Expect(adapter.GetLastSubmittedGraphForTest().expired(),
		"等待之后原生图仍然被额外持有") && ok;

	// ---- ⑤ C ABI 层：字节 / 错误文本 / 错误目标类型 / 已释放句柄都不进入实现 ----
	const uint64_t abiGraphHandle = IScriptRenderBackend_CreateGraph(&adapter);
	ok = Expect(abiGraphHandle != 0u, "C ABI CreateGraph 没有返回句柄") && ok;
	void* abiGraph = IScriptRenderBackend_ResolveHandle(&adapter, abiGraphHandle);
	ok = Expect(abiGraph != nullptr, "C ABI 图句柄解析不到原生对象") && ok;
	if (abiGraph != nullptr)
	{
		std::vector<uint8_t> abiBytes(72u, 0x5Au);
		GPUBufferDescriptorC descriptor{};
		descriptor.count = 3u;
		descriptor.stride = 24u;
		ok = Expect(IScriptRenderGraph_AddBuffer(abiGraph, "AbiVertices", 10u, &descriptor,
			abiBytes.data(), static_cast<uint64_t>(abiBytes.size())) == 0,
			"C ABI AddBuffer 合法调用被拒绝") && ok;
		ok = Expect(IScriptRenderGraph_SetPositionColorStream(abiGraph, "AbiVertices", 10u, 24u,
			0u, 12u) == 0,
			"C ABI SetPositionColorStream 合法调用被拒绝") && ok;
		auto* abiConcrete = dynamic_cast<graphics_backend::CScriptRenderGraph*>(
			static_cast<graphics_backend::IScriptRenderGraph*>(abiGraph));
		ok = Expect(abiConcrete != nullptr &&
			abiConcrete->GetGraphForTest()->GetUploadDataHolder().m_Data.size() == abiBytes.size(),
			"C ABI 的字节没有进入原生上传持有区") && ok;
		ok = Expect(IScriptRenderGraph_AddBuffer(abiGraph, "AbiShort", 8u, &descriptor,
			abiBytes.data(), 71u) != 0,
			"C ABI 的长度不符没有被拒绝") && ok;
		const castl::string abiError = ReadGraphError(abiGraph);
		ok = Expect(Contains(abiError, "data"),
			"C ABI 的错误文本没有点名参数 data") && ok;
		// 错误目标类型：把图句柄当目标传进 Present，必须在句柄层被挡下。
		// 注意判据落在**错误通道**上：生成的 thunk 在参数转换失败时写 1001 并直接 `return {}`，
		// 对 `int32_t` 就是 0（`CodeGenMain.cpp` 不在本 change 的修改范围）。托管表面调完每个方法都会
		// `ThrowIfFailed`，所以 C# 侧看到的是 `IScriptRenderGraphException(1001)`；这里同时断言
		// "实现没有进入"，不靠返回码单独成立。
		const uint32_t passesBeforeWrongType = IScriptRenderGraph_GetPassCount(abiGraph);
		ok = Expect(IScriptRenderGraph_LastErrorCode(abiGraph) == 0,
			"错误目标类型用例开始前错误码不为 0") && ok;
		(void)IScriptRenderGraph_Present(abiGraph, abiGraphHandle);
		ok = Expect(IScriptRenderGraph_LastErrorCode(abiGraph) != 0,
			"把图句柄当渲染目标传进 Present 没有被挡下（错误通道为空）") && ok;
		ok = Expect(IScriptRenderGraph_GetPassCount(abiGraph) == passesBeforeWrongType,
			"错误目标类型仍然改动了图") && ok;
		// 已释放句柄：释放之后再次提交必须被挡下（错误码 1001），且不进入后端。
		const int executeBeforeStale = backend.executeGraphCalls;
		IScriptRenderBackend_ReleaseCreateGraph(&adapter, abiGraphHandle);
		const int32_t staleStatus = IScriptRenderBackend_SubmitGraph(&adapter, abiGraphHandle);
		ok = Expect(staleStatus != 0 || IScriptRenderBackend_LastErrorCode(&adapter) != 0,
			"已释放的图句柄仍能提交（返回 0 且错误通道为空）") && ok;
		ok = Expect(IScriptRenderBackend_LastErrorCode(&adapter) ==
			IScriptRenderBackend_ERROR_STALE_HANDLE,
			"已释放的图句柄没有报 1001") && ok;
		ok = Expect(backend.executeGraphCalls == executeBeforeStale,
			"已释放的图句柄仍进入了后端执行") && ok;
	}

	// 把错误类型的目标句柄交到 GetWindowTarget：必须在句柄层被挡下，不调用后端取窗口句柄。
	{
		const uint64_t captureHandle = IScriptRenderBackend_CreateCaptureTarget(&adapter, 32u, 32u);
		ok = Expect(captureHandle != 0u, "C ABI CreateCaptureTarget 没有返回句柄") && ok;
		const int windowCallsBefore = backend.getWindowHandleCalls;
		ok = Expect(IScriptRenderBackend_GetWindowTarget(&adapter, captureHandle) == 0u,
			"把渲染目标句柄当窗口传进 GetWindowTarget 没有被拒绝") && ok;
		ok = Expect(backend.getWindowHandleCalls == windowCallsBefore,
			"错误类型的句柄进入了 GetWindowTarget 的原生实现") && ok;
		IScriptRenderBackend_ReleaseCreateCaptureTarget(&adapter, captureHandle);
	}

	return ok;
}
