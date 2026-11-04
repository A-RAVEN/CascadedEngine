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
// csharp-render-shader-constants 任务 2.2：参数结构的 C ABI 入口（句柄释放后图仍能执行与等待）。
#include "IScriptShaderStruct.Bindings.cpp"

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
		// 夹具里有的类型才返回真实可写的实例（改前这里固定返回空指针，「参数表含 1 条」会拿空指针
		// 充当绑定 —— csharp-render-shader-constants 任务 1.1 加严）。定义放在夹具类型之后。
		castl::shared_ptr<graphics_backend::ShaderStruct> CreateShaderStruct(
			cacore::NameHash const& structType) override;
		ShaderCompilerSlang::ShaderStructData const* FindShaderStructData(
			cacore::NameHash const& structType) override
		{
			++findShaderStructCalls;
			auto found = m_ShaderStructs.find(structType);
			return found == m_ShaderStructs.end() ? nullptr : &found->second;
		}
		ShaderCompilerSlang::ShaderStructData const* FindShaderRootStructData(
			cacore::PathHash const& shaderPath) override
		{
			++findShaderRootStructCalls;
			auto found = m_ShaderRootStructs.find(shaderPath);
			return found == m_ShaderRootStructs.end() ? nullptr : &found->second;
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
		int createShaderStructCalls = 0;
		int findShaderStructCalls = 0;
		int findShaderRootStructCalls = 0;
		castl::weak_ptr<graphics_backend::GPUGraph> lastGraph;
		// 夹具元数据表（csharp-render-shader-constants 任务 1.1）：适配层的校验查询直接读它。
		// 类型本身在下面定义（`TestShaderStruct` / `fixture`），所以构造与创建都放到类外实现。
		FakeRenderBackend();
		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;
	};

	// ---- 测试用参数结构（csharp-render-shader-constants 任务 1.1）----
	// `ShaderStruct` 是模板 + protected 虚函数，CoreTests 需要一个**真实可写**的实例：它记录每次写入的
	// 字段与值，正例断言"写进去的确实是调用方给的值"，而不是拿空指针充当绑定。
	// 五个 protected 纯虚（值 / 图像 / 缓冲 / 采样器 / 子结构）+ 公开的 `GetStructTypeName` 都要实现。
	class TestShaderStruct final : public graphics_backend::ShaderStruct
	{
	public:
		explicit TestShaderStruct(cacore::NameHash const& typeName) : m_TypeName(typeName) {}

		cacore::NameHash const& GetStructTypeName() const override { return m_TypeName; }

		struct ValueWrite
		{
			cacore::NameHash name;
			uint32_t sizeInBytes = 0;
			uint32_t elementIndex = 0;
			std::vector<uint8_t> bytes;
		};
		struct StructWrite
		{
			cacore::NameHash name;
			castl::shared_ptr<graphics_backend::ShaderStruct> sub;
			uint32_t elementIndex = 0;
		};

		std::vector<ValueWrite> valueWrites;
		std::vector<StructWrite> structWrites;

	protected:
		void SetValueInternal(cacore::NameHash const& name, void const* pValue, uint32_t sizeInBytes,
			uint32_t elementIndex) override
		{
			ValueWrite write;
			write.name = name;
			write.sizeInBytes = sizeInBytes;
			write.elementIndex = elementIndex;
			auto const* bytes = static_cast<uint8_t const*>(pValue);
			write.bytes.assign(bytes, bytes + sizeInBytes);
			valueWrites.push_back(write);
		}
		void SetImageInternal(cacore::NameHash const&, graphics_backend::ImageHandle const&,
			graphics_backend::GPUTextureView const&, uint32_t) override
		{
		}
		void SetBufferInternal(cacore::NameHash const&, graphics_backend::BufferHandle const&,
			uint32_t) override
		{
		}
		void SetSamplerInternal(cacore::NameHash const&, graphics_backend::TextureSamplerDescriptor const&,
			uint32_t) override
		{
		}
		void SetStructInternal(cacore::NameHash const& name,
			castl::shared_ptr<graphics_backend::ShaderStruct> const& subStruct,
			uint32_t elementIndex) override
		{
			structWrites.push_back(StructWrite{ name, subStruct, elementIndex });
		}

	private:
		cacore::NameHash m_TypeName;
	};

	// ---- 夹具元数据表（csharp-render-shader-constants 任务 1.1）----
	// 数值全部来自实际编译元数据（`CAResources/Shaders/Test/TestTriangleWithConstantColor.slang`）：
	// `ConstantColor { ColorSubStruct colorStruct[3] }`、`ColorSubStruct { float3 color }`。
	// `float3` 的类型名不是猜的：从 `CAAssets/D3D12ShaderLibrary.shLib` 反序列化出来的元素表里，
	// `ColorSubStruct` 的 `color` 字段类型名逐字是 `vector<float,3>`（Slang `getFullName()` 的输出）。
	std::string const kSlangFloat3TypeName = "vector<float,3>";
	cacore::NameHash const kFloat3TypeName(kSlangFloat3TypeName.c_str());

	namespace fixture
	{
		bool AddUniform(ShaderCompilerSlang::ShaderStructData& target, char const* typeName, char const* name,
			uint32_t offset, uint32_t elementMemorySize, uint32_t stride, uint32_t elementCount)
		{
			ShaderCompilerSlang::UniformElement element;
			element.Init(cacore::NameHash(typeName), cacore::NameHash(name), offset, elementMemorySize, stride,
				elementCount);
			target.m_StructUniforms.EnsureElement(element);
			return true;
		}

		bool AddSubStruct(ShaderCompilerSlang::ShaderStructData& target, char const* name,
			char const* structTypeName, uint32_t elementCount, uint32_t offset, uint32_t elementMemorySize,
			uint32_t stride)
		{
			ShaderCompilerSlang::SubStructReference reference{};
			reference.m_Name = cacore::NameHash(name);
			reference.m_StructTypeName = cacore::NameHash(structTypeName);
			reference.m_ElementCount = elementCount;
			reference.m_MemoryOffset = offset;
			reference.m_ElementMemorySize = elementMemorySize;
			reference.m_Stride = stride;
			target.EnsureSubStructReference(reference);
			return true;
		}

		castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> BuildStructs()
		{
			castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> table;

			// ColorSubStruct：一个 12 字节 float3 单元素字段。
			ShaderCompilerSlang::ShaderStructData colorSubStruct;
			colorSubStruct.Init(CANAME("ColorSubStruct"));
			colorSubStruct.m_StructUniforms.SetSize(12u, 12u);
			AddUniform(colorSubStruct, kSlangFloat3TypeName.c_str(), "color", 0u, 12u, 12u, 1u);
			table.insert(castl::make_pair(CANAME("ColorSubStruct"), colorSubStruct));

			// ConstantColor：colorStruct[3] 三个 ColorSubStruct。
			ShaderCompilerSlang::ShaderStructData constantColor;
			constantColor.Init(CANAME("ConstantColor"));
			constantColor.m_StructUniforms.SetSize(36u, 12u);
			AddSubStruct(constantColor, "colorStruct", "ColorSubStruct", 3u, 0u, 12u, 12u);
			table.insert(castl::make_pair(CANAME("ConstantColor"), constantColor));

			// 负例素材：12 字节但不是 float3（`int3` → `vector<int,3>`）。
			ShaderCompilerSlang::ShaderStructData intFieldStruct;
			intFieldStruct.Init(CANAME("IntFieldStruct"));
			intFieldStruct.m_StructUniforms.SetSize(12u, 12u);
			AddUniform(intFieldStruct, "vector<int,3>", "value", 0u, 12u, 12u, 1u);
			table.insert(castl::make_pair(CANAME("IntFieldStruct"), intFieldStruct));

			// 负例素材：`float3[2]` 的数组元素（12 字节，但 m_ElementCount == 2）。
			ShaderCompilerSlang::ShaderStructData float3ArrayStruct;
			float3ArrayStruct.Init(CANAME("Float3ArrayStruct"));
			float3ArrayStruct.m_StructUniforms.SetSize(24u, 12u);
			AddUniform(float3ArrayStruct, kSlangFloat3TypeName.c_str(), "colors", 0u, 12u, 12u, 2u);
			table.insert(castl::make_pair(CANAME("Float3ArrayStruct"), float3ArrayStruct));

			// 负例素材：另一个子结构类型（与 ColorSubStruct 形状相同、类型名不同）。
			ShaderCompilerSlang::ShaderStructData otherSubStruct;
			otherSubStruct.Init(CANAME("OtherSubStruct"));
			otherSubStruct.m_StructUniforms.SetSize(12u, 12u);
			AddUniform(otherSubStruct, kSlangFloat3TypeName.c_str(), "color", 0u, 12u, 12u, 1u);
			table.insert(castl::make_pair(CANAME("OtherSubStruct"), otherSubStruct));

			return table;
		}

		castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> BuildRootStructs()
		{
			castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> table;
			// 常量色着色器的根结构：一个 `ParameterBlock<ConstantColor> constantColorBlock`。
			ShaderCompilerSlang::ShaderStructData root;
			root.Init(CANAME("__RootType"));
			AddSubStruct(root, "constantColorBlock", "ConstantColor", 1u, 0u, 36u, 36u);
			table.insert(castl::make_pair(CAPATH("Shaders/Test/TestTriangleWithConstantColor"), root));
			return table;
		}
	}

	// 夹具元数据表的构造与"合法类型返回真实实例"都放在这里：`TestShaderStruct` 与 `fixture` 这时
	// 已经定义完，类内联实现放在前面会引用不到它们。
	FakeRenderBackend::FakeRenderBackend()
		: m_ShaderStructs(fixture::BuildStructs())
		, m_ShaderRootStructs(fixture::BuildRootStructs())
	{
	}

	castl::shared_ptr<graphics_backend::ShaderStruct> FakeRenderBackend::CreateShaderStruct(
		cacore::NameHash const& structType)
	{
		++createShaderStructCalls;
		if (m_ShaderStructs.find(structType) == m_ShaderStructs.end())
		{
			return nullptr;
		}
		return castl::make_shared<TestShaderStruct>(structType);
	}

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
		int32_t SetPositionStream(cacore::NameHash const&, uint32_t, uint32_t) override { return 0; }
		int32_t SetPassParam(uint32_t, cacore::NameHash const&,
			castl::shared_ptr<graphics_backend::IScriptShaderStruct> const&) override
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

	// ---- ⑥ 着色器参数结构（csharp-render-shader-constants 任务 2.1）----
	// 全部校验都必须在适配层、进入后端之前完成，并且拒绝要逐项点名参数。
	{
		auto* scriptAdapter = &adapter;
		auto* backendPtr = &backend;

		// 本段自带一张图：④ 里已经把 `graph` 释放掉（`graph.reset()`），旧指针不能再用。
		auto paramGraph = scriptAdapter->CreateGraph();
		auto* paramGraphConcrete =
			dynamic_cast<graphics_backend::CScriptRenderGraph*>(paramGraph.get());
		ok = Expect(paramGraph != nullptr && paramGraphConcrete != nullptr,
			"参数结构用例的图没有创建出来") && ok;
		ok = Expect(paramGraph->AddBuffer(CANAME("ParamVertices"),
			graphics_backend::GPUBufferDescriptor::Create(3u, 12u), std::vector<uint8_t>(36u, 0u)) == 0,
			"参数结构用例的 AddBuffer 失败") && ok;
		ok = Expect(paramGraph->SetPositionStream(CANAME("ParamStream"), 12u, 0u) == 0,
			"SetPositionStream 合法调用被拒绝") && ok;
		ok = Expect(paramGraph->AddTrianglePass(capture,
			CAPATH("Shaders/Test/TestTriangleWithConstantColor"), CANAME("ParamStream"),
			CANAME("ParamVertices"), 3u, 0.0f, 0.0f, 1.0f, 1.0f) == 0,
			"参数结构用例的 AddTrianglePass 失败") && ok;

		// 未知类型：空句柄 + 错误文本点名类型名 + 后端创建调用次数为 0。
		const int createCallsBefore = backendPtr->createShaderStructCalls;
		auto unknown = scriptAdapter->CreateShaderStruct(CANAME("NoSuchStruct"));
		ok = Expect(unknown == nullptr, "未知类型名创建出了参数结构") && ok;
		ok = Expect(Contains(scriptAdapter->GetLastError(), "NoSuchStruct"),
			"未知类型名的错误文本没有点名结构类型名") && ok;
		ok = Expect(backendPtr->createShaderStructCalls == createCallsBefore,
			"未知类型名进入了后端 CreateShaderStruct") && ok;
		std::printf("  [参数结构] 未知类型 status=空句柄 点名=%d 后端创建=%d\n",
			Contains(scriptAdapter->GetLastError(), "NoSuchStruct") ? 1 : 0,
			backendPtr->createShaderStructCalls - createCallsBefore);

		// 合法类型：窄对象 + 真实 ShaderStruct；成功调用清空旧错误。
		auto constantColor = scriptAdapter->CreateShaderStruct(CANAME("ConstantColor"));
		ok = Expect(constantColor != nullptr, "合法类型名没创建出参数结构") && ok;
		ok = Expect(scriptAdapter->GetLastError().empty(), "成功创建没有清空旧错误") && ok;
		auto colorSub0 = scriptAdapter->CreateShaderStruct(CANAME("ColorSubStruct"));
		auto colorSub1 = scriptAdapter->CreateShaderStruct(CANAME("ColorSubStruct"));
		auto colorSub2 = scriptAdapter->CreateShaderStruct(CANAME("ColorSubStruct"));
		auto otherSub = scriptAdapter->CreateShaderStruct(CANAME("OtherSubStruct"));
		auto intField = scriptAdapter->CreateShaderStruct(CANAME("IntFieldStruct"));
		auto float3Array = scriptAdapter->CreateShaderStruct(CANAME("Float3ArrayStruct"));
		ok = Expect(colorSub0 != nullptr && colorSub1 != nullptr && colorSub2 != nullptr &&
			otherSub != nullptr && intField != nullptr && float3Array != nullptr,
			"夹具里的合法类型有一个创建失败") && ok;
		ok = Expect(scriptAdapter->GetCreateShaderStructCountForTest() == 7u,
			"参数结构创建计数不对（7 个合法类型）") && ok;

		// 正例：写 float3 与挂子结构都要落到真实 ShaderStruct 上（字段与值逐项核对）。
		const uint32_t float3CallsBefore = scriptAdapter->GetSetFloat3CountForTest();
		const uint32_t structCallsBefore = scriptAdapter->GetSetStructCountForTest();
		ok = Expect(colorSub0->SetFloat3(CANAME("color"), 1.0f, 0.0f, 0.0f) == 0,
			"SetFloat3 合法调用被拒绝") && ok;
		ok = Expect(colorSub1->SetFloat3(CANAME("color"), 0.0f, 1.0f, 0.0f) == 0,
			"SetFloat3 合法调用被拒绝（第二个子结构）") && ok;
		ok = Expect(colorSub2->SetFloat3(CANAME("color"), 0.0f, 0.0f, 1.0f) == 0,
			"SetFloat3 合法调用被拒绝（第三个子结构）") && ok;
		ok = Expect(scriptAdapter->GetSetFloat3CountForTest() == float3CallsBefore + 3u,
			"SetFloat3 计数没有随成功调用增长") && ok;
		ok = Expect(constantColor->SetStruct(CANAME("colorStruct"), colorSub0, 0u) == 0 &&
			constantColor->SetStruct(CANAME("colorStruct"), colorSub1, 1u) == 0 &&
			constantColor->SetStruct(CANAME("colorStruct"), colorSub2, 2u) == 0,
			"SetStruct 合法调用被拒绝") && ok;
		ok = Expect(scriptAdapter->GetSetStructCountForTest() == structCallsBefore + 3u,
			"SetStruct 计数没有随成功调用增长") && ok;
		// 真实写入核对：字段名、字节数、元素下标与三个 float 的值都必须逐项对上。
		auto* colorSubConcrete = dynamic_cast<graphics_backend::CScriptShaderStruct*>(colorSub2.get());
		ok = Expect(colorSubConcrete != nullptr, "参数结构不是本适配面的对象") && ok;
		if (colorSubConcrete != nullptr)
		{
			auto const* testStruct = dynamic_cast<TestShaderStruct*>(
				colorSubConcrete->GetShaderStructForTest().get());
			ok = Expect(testStruct != nullptr, "参数结构里的真实 ShaderStruct 不是测试桩") && ok;
			if (testStruct != nullptr)
			{
				ok = Expect(testStruct->valueWrites.size() == 1u,
					"SetFloat3 没有在真实 ShaderStruct 上写一次值") && ok;
				if (testStruct->valueWrites.size() == 1u)
				{
					auto const& write = testStruct->valueWrites.front();
					float values[3] = {};
					if (write.bytes.size() == sizeof(values))
					{
						std::memcpy(values, write.bytes.data(), sizeof(values));
					}
					ok = Expect(write.name == CANAME("color") && write.sizeInBytes == 12u &&
						write.elementIndex == 0u && values[0] == 0.0f && values[1] == 0.0f &&
						values[2] == 1.0f,
						"写入字段 / 字节数 / 下标 / 值有一项不符") && ok;
				}
			}
		}
		auto* constantColorConcrete =
			dynamic_cast<graphics_backend::CScriptShaderStruct*>(constantColor.get());
		if (constantColorConcrete != nullptr)
		{
			auto const* testStruct = dynamic_cast<TestShaderStruct*>(
				constantColorConcrete->GetShaderStructForTest().get());
			ok = Expect(testStruct != nullptr && testStruct->structWrites.size() == 3u,
				"SetStruct 没有在真实 ShaderStruct 上挂三次子结构") && ok;
			if (testStruct != nullptr && testStruct->structWrites.size() == 3u)
			{
				bool sameObjects = true;
				for (size_t index = 0; index < 3u; ++index)
				{
					auto* expected = dynamic_cast<graphics_backend::CScriptShaderStruct*>(
						(index == 0u ? colorSub0 : (index == 1u ? colorSub1 : colorSub2)).get());
					sameObjects = sameObjects && expected != nullptr &&
						testStruct->structWrites[index].sub == expected->GetShaderStructForTest() &&
						testStruct->structWrites[index].elementIndex == static_cast<uint32_t>(index) &&
						testStruct->structWrites[index].name == CANAME("colorStruct");
				}
				ok = Expect(sameObjects, "挂上去的子结构对象 / 下标 / 字段名有一项不符") && ok;
			}
		}

		// 拒绝路径：每一项都返回非零、点名参数，且不写进真实 ShaderStruct。
		// 两个入参都是**动作**而不是值：C++ 的函数实参求值顺序未指定（MSVC 从右往左），
		// 写成 `expectReject(call(), obj->GetLastError(), …)` 会先取上一次的错误文本（实测踩过）。
		auto expectReject = [&ok](auto const& action, auto const& errorText, char const* token,
			char const* what) {
			const int32_t status = action();
			const castl::string text = errorText();
			const bool statusOk = Expect(status != 0, what);
			const bool textOk = Expect(Contains(text, token), what);
			std::printf("  [参数结构拒绝] %-28s status=%d 点名=%s 文本=%s\n", what, status,
				token, text.c_str());
			ok = statusOk && textOk && ok;
		};
		const size_t valuesBefore = [] (graphics_backend::CScriptShaderStruct* s) {
			auto* t = s == nullptr ? nullptr
				: dynamic_cast<TestShaderStruct*>(s->GetShaderStructForTest().get());
			return t == nullptr ? size_t(0) : t->valueWrites.size();
		}(colorSubConcrete);
		const int createCallsBeforeFields = backendPtr->createShaderStructCalls;

		// 未知字段
		expectReject([&] { return colorSub0->SetFloat3(CANAME("noSuchField"), 1.0f, 0.0f, 0.0f); },
			[&] { return colorSub0->GetLastError(); }, "noSuchField", "未知字段");
		// 12 字节但不是 float3（int3）：类型不符必须同时点名期望与实际类型名
		expectReject([&] { return intField->SetFloat3(CANAME("value"), 1.0f, 0.0f, 0.0f); },
			[&] { return intField->GetLastError(); }, "vector<int,3>", "12 字节 int3 类型不符");
		ok = Expect(Contains(intField->GetLastError(), "vector<float,3>"),
			"类型不符没有点名期望类型 vector<float,3>") && ok;
		// float3[2] 的数组元素（12 字节但 m_ElementCount == 2）
		expectReject([&] { return float3Array->SetFloat3(CANAME("colors"), 1.0f, 0.0f, 0.0f); },
			[&] { return float3Array->GetLastError(); }, "colors", "float3[2] 数组字段");
		// 空子结构
		expectReject([&] { return constantColor->SetStruct(CANAME("colorStruct"), nullptr, 0u); },
			[&] { return constantColor->GetLastError(); }, "sub", "空子结构");
		// 类型不符的子结构：同时点名期望与实际类型名
		expectReject([&] { return constantColor->SetStruct(CANAME("colorStruct"), otherSub, 0u); },
			[&] { return constantColor->GetLastError(); }, "OtherSubStruct", "子结构类型不符");
		ok = Expect(Contains(constantColor->GetLastError(), "ColorSubStruct"),
			"子结构类型不符没有点名期望类型 ColorSubStruct") && ok;
		// 越界下标
		expectReject([&] { return constantColor->SetStruct(CANAME("colorStruct"), colorSub0, 3u); },
			[&] { return constantColor->GetLastError(); }, "elementIndex", "越界 elementIndex");
		// 未知块名 / 越界 passIndex / 空参数 / 未登记着色器路径
		expectReject([&] { return paramGraph->SetPassParam(0u, CANAME("noSuchBlock"), constantColor); },
			[&] { return paramGraph->GetLastError(); }, "noSuchBlock", "未知参数块名");
		expectReject([&] { return paramGraph->SetPassParam(7u, CANAME("constantColorBlock"), constantColor); },
			[&] { return paramGraph->GetLastError(); }, "passIndex", "越界 passIndex");
		expectReject([&] { return paramGraph->SetPassParam(0u, CANAME("constantColorBlock"), nullptr); },
			[&] { return paramGraph->GetLastError(); }, "params", "空参数");
		// 块类型不符：把 ColorSubStruct 绑到 constantColorBlock（根结构里写的是 ConstantColor）
		expectReject([&] { return paramGraph->SetPassParam(0u, CANAME("constantColorBlock"), colorSub0); },
			[&] { return paramGraph->GetLastError(); }, "ColorSubStruct", "参数块类型不符");
		ok = Expect(Contains(paramGraph->GetLastError(), "ConstantColor"),
			"参数块类型不符没有点名期望类型 ConstantColor") && ok;
		// 未登记的着色器路径：另建一张图，用一个没有根结构登记的路径
		{
			auto unregistered = adapter.CreateGraph();
			ok = Expect(unregistered != nullptr, "CreateGraph 返回空") && ok;
			ok = Expect(unregistered->AddBuffer(CANAME("URBuffer"),
				graphics_backend::GPUBufferDescriptor::Create(3u, 12u),
				std::vector<uint8_t>(36u, 0u)) == 0, "未登记路径用例的 AddBuffer 失败") && ok;
			ok = Expect(unregistered->SetPositionStream(CANAME("URStream"), 12u, 0u) == 0,
				"SetPositionStream 合法调用被拒绝") && ok;
			ok = Expect(unregistered->AddTrianglePass(capture,
				CAPATH("Shaders/Test/NoSuchShader"), CANAME("URStream"), CANAME("URBuffer"), 3u,
				0.0f, 0.0f, 1.0f, 1.0f) == 0, "未登记路径用例的 AddTrianglePass 失败") && ok;
			expectReject([&] { return unregistered->SetPassParam(0u, CANAME("constantColorBlock"), constantColor); },
				[&] { return unregistered->GetLastError(); }, "Shaders/Test/NoSuchShader", "未登记着色器路径");
			// 合法路径 + 合法块 + 合法类型：成功，并清空旧错误。
			ok = Expect(paramGraph->SetPassParam(0u, CANAME("constantColorBlock"), constantColor) == 0,
				"SetPassParam 合法调用被拒绝") && ok;
			ok = Expect(paramGraph->GetLastError().empty(), "SetPassParam 成功没有清空旧错误") && ok;
			// 写入位置核对：真实 pass 的参数表里就是这一个对象（2.2 的同一对象判据）
			auto const& passParams =
				paramGraphConcrete->GetGraphForTest()->GetRenderPasses()[0].GetShaderStructs();
			ok = Expect(passParams.size() == 1u &&
				passParams.begin()->first == CANAME("constantColorBlock") &&
				passParams.begin()->second != nullptr &&
				constantColorConcrete != nullptr &&
				passParams.begin()->second == constantColorConcrete->GetShaderStructForTest(),
				"真实 pass 的参数表不是「1 条 constantColorBlock 且与窄对象同一实例」") && ok;
			if (passParams.size() == 1u && constantColorConcrete != nullptr)
			{
				std::printf("  [参数表同一实例] 表内指针=%p 夹具实例指针=%p 一致=%d\n",
					static_cast<void*>(passParams.begin()->second.get()),
					static_cast<void*>(constantColorConcrete->GetShaderStructForTest().get()),
					passParams.begin()->second == constantColorConcrete->GetShaderStructForTest() ? 1 : 0);
			}
		}
		// 拒绝没有写进真实 ShaderStruct，也没有再创建参数结构。
		ok = Expect([] (graphics_backend::CScriptShaderStruct* s) {
				auto* t = s == nullptr ? nullptr
					: dynamic_cast<TestShaderStruct*>(s->GetShaderStructForTest().get());
				return t == nullptr ? size_t(0) : t->valueWrites.size();
			}(colorSubConcrete) == valuesBefore,
			"被拒绝的 SetFloat3 仍然写进了真实 ShaderStruct") && ok;
		ok = Expect(backendPtr->createShaderStructCalls == createCallsBeforeFields,
			"拒绝路径仍然创建了参数结构") && ok;

		// 重复流名：两种流都必须在登记前拒绝并点名 streamName，pass 数不变、后端调用 0 次。
		{
			const uint32_t passesBefore = paramGraph->GetPassCount();
			const int executeBefore = backendPtr->executeGraphCalls;
			ok = Expect(paramGraph->SetPositionColorStream(CANAME("ParamStream"), 24u, 0u, 12u) != 0 &&
				Contains(paramGraph->GetLastError(), "streamName"),
				"重复登记两属性流没有被拒绝并点名 streamName") && ok;
			ok = Expect(paramGraph->SetPositionStream(CANAME("ParamStream"), 12u, 0u) != 0 &&
				Contains(paramGraph->GetLastError(), "streamName"),
				"重复登记单 POSITION 流没有被拒绝并点名 streamName") && ok;
			ok = Expect(paramGraph->GetPassCount() == passesBefore &&
				backendPtr->executeGraphCalls == executeBefore,
				"重复流名的拒绝改动了图或进入了后端") && ok;
			std::printf("  [流名查重] 两属性 / 单 POSITION 都拒绝，pass=%u execute=%d\n",
				paramGraph->GetPassCount(), backendPtr->executeGraphCalls);
		}
	}

	// ---- ⑦ 提交前必需参数块检查、参数保活与 C ABI 句柄释放（csharp-render-shader-constants 任务 2.2）----
	// 五条判据：真实 pass 参数表同一实例（⑥）、缺参数块提交被拒且执行次数 0、绑定后提交成功、
	// 提交后释放 C ABI 参数结构句柄仍能执行与等待、等待后待完成集合清理。
	{
		auto targetA = adapter.CreateCaptureTarget(64u, 48u);
		auto targetB = adapter.CreateCaptureTarget(64u, 48u);
		ok = Expect(targetA != nullptr && targetB != nullptr, "任务 2.2 的离屏目标创建失败") && ok;
		auto submitGraph = adapter.CreateGraph();
		auto* submitConcrete = dynamic_cast<graphics_backend::CScriptRenderGraph*>(submitGraph.get());
		ok = Expect(submitConcrete != nullptr, "任务 2.2 的图没有创建出来") && ok;
		ok = Expect(submitGraph->AddBuffer(CANAME("SubmitVertices"),
			graphics_backend::GPUBufferDescriptor::Create(3u, 12u), std::vector<uint8_t>(36u, 0u)) == 0,
			"任务 2.2 的 AddBuffer 失败") && ok;
		ok = Expect(submitGraph->SetPositionStream(CANAME("SubmitStream"), 12u, 0u) == 0,
			"任务 2.2 的 SetPositionStream 失败") && ok;
		ok = Expect(submitGraph->AddTrianglePass(targetA,
				CAPATH("Shaders/Test/TestTriangleWithConstantColor"), CANAME("SubmitStream"),
				CANAME("SubmitVertices"), 3u, 0.0f, 0.0f, 1.0f, 1.0f) == 0 &&
			submitGraph->AddTrianglePass(targetB, CAPATH("Shaders/Test/TestTriangleWithConstantColor"),
				CANAME("SubmitStream"), CANAME("SubmitVertices"), 3u, 0.0f, 0.0f, 1.0f, 1.0f) == 0,
			"任务 2.2 的两个 pass 没有加出来") && ok;
		auto submitParams = adapter.CreateShaderStruct(CANAME("ConstantColor"));
		ok = Expect(submitParams != nullptr, "任务 2.2 的参数结构创建失败") && ok;
		for (uint32_t slot = 0; slot < 3u; ++slot)
		{
			auto sub = adapter.CreateShaderStruct(CANAME("ColorSubStruct"));
			ok = Expect(sub != nullptr && sub->SetFloat3(CANAME("color"), 1.0f, 0.0f, 0.0f) == 0 &&
				submitParams->SetStruct(CANAME("colorStruct"), sub, slot) == 0,
				"任务 2.2 的子结构写入失败") && ok;
		}
		// skip-param（两个 pass 都没绑）：提交必须在进入后端之前被拒绝、点名块名、执行次数 0、
		// 也不许把图放进待完成集合。
		const uint32_t executeBeforeMissing = adapter.GetExecuteGraphCountForTest();
		const int backendExecuteBeforeMissing = backend.executeGraphCalls;
		const int32_t missingStatus = adapter.SubmitGraph(submitGraph);
		const castl::string missingText = adapter.GetLastError();
		ok = Expect(missingStatus != 0, "缺参数块的提交没有返回非零") && ok;
		ok = Expect(Contains(missingText, "constantColorBlock"),
			"缺参数块的错误文本没有点名 constantColorBlock") && ok;
		ok = Expect(adapter.GetExecuteGraphCountForTest() == executeBeforeMissing,
			"缺参数块的提交进入了后端执行") && ok;
		ok = Expect(backend.executeGraphCalls == backendExecuteBeforeMissing,
			"缺参数块的提交调用了后端 ExecuteGraph") && ok;
		ok = Expect(adapter.GetPendingGraphCountForTest() == 0u,
			"缺参数块的提交把图放进了待完成集合") && ok;
		std::printf("  [必需块检查] status=%d 执行次数=%u 待完成=%u 文本=%s\n", missingStatus,
			adapter.GetExecuteGraphCountForTest(), adapter.GetPendingGraphCountForTest(),
			missingText.c_str());

		// 两个 pass 都绑好之后再提交：成功、执行次数 +1、待完成集合持有图。
		ok = Expect(submitGraph->SetPassParam(0u, CANAME("constantColorBlock"), submitParams) == 0 &&
			submitGraph->SetPassParam(1u, CANAME("constantColorBlock"), submitParams) == 0,
			"两个 pass 的参数绑定失败") && ok;
		ok = Expect(adapter.GetSetPassParamCountForTest() >= 2u,
			"SetPassParam 成功计数没有随两次绑定增长") && ok;
		ok = Expect(adapter.SubmitGraph(submitGraph) == 0, "绑定完整后的提交失败") && ok;
		ok = Expect(adapter.GetExecuteGraphCountForTest() == executeBeforeMissing + 1u,
			"成功提交没有让执行次数 +1") && ok;
		ok = Expect(adapter.GetPendingGraphCountForTest() == 1u,
			"待完成集合没有持有图强引用") && ok;

		// 参数保活：真实 pass 的参数表里就是那个夹具实例（指针相等），不拿空指针充当绑定。
		auto* submitParamsConcrete =
			dynamic_cast<graphics_backend::CScriptShaderStruct*>(submitParams.get());
		ok = Expect(submitParamsConcrete != nullptr, "任务 2.2 的参数结构不是本适配面对象") && ok;
		if (submitConcrete != nullptr && submitParamsConcrete != nullptr)
		{
			auto const& boundA =
				submitConcrete->GetGraphForTest()->GetRenderPasses()[0].GetShaderStructs();
			auto const& boundB =
				submitConcrete->GetGraphForTest()->GetRenderPasses()[1].GetShaderStructs();
			ok = Expect(boundA.size() == 1u && boundB.size() == 1u &&
				boundA.begin()->first == CANAME("constantColorBlock") &&
				boundA.begin()->second != nullptr &&
				boundA.begin()->second == submitParamsConcrete->GetShaderStructForTest() &&
				boundB.begin()->second == submitParamsConcrete->GetShaderStructForTest(),
				"两个 pass 的参数表不是「各 1 条 constantColorBlock 且与夹具实例同一对象」") && ok;
			if (boundA.size() == 1u && boundB.size() == 1u)
			{
				std::printf("  [参数保活] pass0 表内指针=%p pass1 表内指针=%p 夹具实例指针=%p\n",
					static_cast<void*>(boundA.begin()->second.get()),
					static_cast<void*>(boundB.begin()->second.get()),
					static_cast<void*>(submitParamsConcrete->GetShaderStructForTest().get()));
			}
		}
		adapter.WaitIdle();
		ok = Expect(adapter.GetPendingGraphCountForTest() == 0u,
			"等待之后待完成集合没有清理") && ok;

		// C ABI 层：提交后释放参数结构句柄，图仍能执行与等待（图通过 pass 的 shaderStructs
		// 持有真实 ShaderStruct，不依赖窄句柄）。
		const uint64_t abiGraph = IScriptRenderBackend_CreateGraph(&adapter);
		ok = Expect(abiGraph != 0u, "C ABI CreateGraph 没有返回句柄") && ok;
		void* abiGraphPtr = IScriptRenderBackend_ResolveHandle(&adapter, abiGraph);
		const uint64_t abiTarget = IScriptRenderBackend_CreateCaptureTarget(&adapter, 32u, 32u);
		ok = Expect(abiGraphPtr != nullptr && abiTarget != 0u, "C ABI 图或离屏目标没有创建出来") && ok;
		if (abiGraphPtr != nullptr)
		{
			std::vector<uint8_t> abiBytes(36u, 0u);
			GPUBufferDescriptorC abiDesc{};
			abiDesc.count = 3u;
			abiDesc.stride = 12u;
			ok = Expect(IScriptRenderGraph_AddBuffer(abiGraphPtr, "AbiParamVertices", 16u, &abiDesc,
				abiBytes.data(), 36u) == 0, "C ABI AddBuffer 失败") && ok;
			ok = Expect(IScriptRenderGraph_SetPositionStream(abiGraphPtr, "AbiParamStream", 14u, 12u, 0u) == 0,
				"C ABI SetPositionStream 失败") && ok;
			ok = Expect(IScriptRenderGraph_AddTrianglePass(abiGraphPtr, abiTarget,
				"Shaders/Test/TestTriangleWithConstantColor", 42u, "AbiParamStream", 14u,
				"AbiParamVertices", 16u, 3u, 0.0f, 0.0f, 1.0f, 1.0f) == 0,
				"C ABI AddTrianglePass 失败") && ok;

			const uint64_t abiParams = IScriptRenderBackend_CreateShaderStruct(&adapter, "ConstantColor", 13u);
			ok = Expect(abiParams != 0u, "C ABI CreateShaderStruct 没有返回参数结构句柄") && ok;
			void* abiParamsPtr = IScriptRenderBackend_ResolveHandle(&adapter, abiParams);
			ok = Expect(abiParamsPtr != nullptr, "C ABI 参数结构句柄解析不到实例") && ok;
			for (uint32_t slot = 0; abiParamsPtr != nullptr && slot < 3u; ++slot)
			{
				const uint64_t abiSub = IScriptRenderBackend_CreateShaderStruct(&adapter, "ColorSubStruct", 14u);
				void* abiSubPtr = IScriptRenderBackend_ResolveHandle(&adapter, abiSub);
				ok = Expect(abiSub != 0u && abiSubPtr != nullptr, "C ABI 子结构句柄创建失败") && ok;
				if (abiSubPtr == nullptr)
				{
					break;
				}
				ok = Expect(IScriptShaderStruct_SetFloat3(abiSubPtr, "color", 5u, 1.0f, 0.0f, 0.0f) == 0,
					"C ABI SetFloat3 失败") && ok;
				ok = Expect(IScriptShaderStruct_SetStruct(abiParamsPtr, "colorStruct", 11u, abiSub, slot) == 0,
					"C ABI SetStruct 失败") && ok;
			}
			ok = Expect(IScriptRenderGraph_SetPassParam(abiGraphPtr, 0u, "constantColorBlock", 18u,
				abiParams) == 0, "C ABI SetPassParam 失败") && ok;
			const uint32_t executeBeforeAbi = adapter.GetExecuteGraphCountForTest();
			ok = Expect(IScriptRenderBackend_SubmitGraph(&adapter, abiGraph) == 0,
				"C ABI SubmitGraph 失败") && ok;
			ok = Expect(adapter.GetExecuteGraphCountForTest() == executeBeforeAbi + 1u,
				"C ABI 提交没有让执行次数 +1") && ok;
			// 提交之后释放参数结构句柄：图仍能等待，等待后待完成集合清空。
			const uint32_t pendingAfterAbiSubmit = adapter.GetPendingGraphCountForTest();
			IScriptRenderBackend_ReleaseCreateShaderStruct(&adapter, abiParams);
			adapter.WaitIdle();
			const uint32_t executeAfterAbiWait = adapter.GetExecuteGraphCountForTest();
			ok = Expect(pendingAfterAbiSubmit == 1u &&
				adapter.GetPendingGraphCountForTest() == 0u &&
				executeAfterAbiWait == executeBeforeAbi + 1u,
				"释放 C ABI 参数结构句柄之后图不能执行与等待（或待完成集合没有清理）") && ok;
			std::printf("  [参数句柄释放] 提交后待完成=%u → 等待后=%u，执行次数=%u（释放句柄前后不变）\n",
				pendingAfterAbiSubmit, adapter.GetPendingGraphCountForTest(), executeAfterAbiWait);
			IScriptRenderBackend_ReleaseCreateGraph(&adapter, abiGraph);
		}
	}

	return ok;
}
