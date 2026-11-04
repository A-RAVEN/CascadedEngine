#pragma once
// 脚本窄渲染适配面的原生实现（csharp-render-backend-triangle 任务 2.1 / 2.2）。
//
// 三个实现类只在引擎模块内部使用：脚本侧只能看到 `ScriptRenderBridge.h` 里的接口。
// 具体建图、调度器创建、后端提交与资源保活都在这里完成，公开表面不出现任何建图类或原生指针。
#include "../header/ScriptRenderBridge.h"

#include <GPUGraph.h>

#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

namespace thread_management
{
	class CThreadManager;
}

namespace graphics_backend
{
	class CRenderBackend;

	// 参数结构窄对象的计数槽（csharp-render-shader-constants 任务 2.1 / 3.2）：三个 `*ForTest` 钩子要从
	// 适配器上读到「创建了几个参数结构 / 写了多少次 float3 / 挂了几次子结构」。计数放在后端持有的
	// 一个小结构里、窄对象只拿指针 —— 避免窄对象反向持有后端（那会形成环形引用）。
	// `setPassParam` 由任务 3.2 的读数要求补上（「两次 SetPassParam 的对应断言」必须来自原生钩子），
	// 它是实现类上的测试钩子，不进任何绑定接口，不影响审计表的 90 方法口径。
	struct ScriptShaderCallCounters
	{
		uint32_t createShaderStruct = 0;
		uint32_t setFloat3 = 0;
		uint32_t setStruct = 0;
		// csharp-render-structured-buffer 任务 2.1：成功绑定结构化缓冲字段的次数（正常探针增量 +1、
		// `unbound-field` 增量 0）。
		uint32_t setBuffer = 0;
		uint32_t setPassParam = 0;
		// csharp-render-structured-buffer 任务 2.1：`CScriptRenderGraph::AddBuffer` 成功登记的次数。
		// 「非探针期建缓冲为 0」必须有这个读数（适配器现状没有别的计数能区分）。
		uint32_t addBuffer = 0;
		// csharp-render-texture-sampling 任务 2.1 / 2.2：成功 `CreateTexture`、成功 `SetImage`、
		// 成功 `SetSampler` 的次数（正常探针各 +1，`unbound-image` 的 SetImage 增量 0 等判据用它）。
		uint32_t createTexture = 0;
		uint32_t setImage = 0;
		uint32_t setSampler = 0;
	};

	// 只读纹理窄对象（csharp-render-texture-sampling 任务 2.1）：只报宽高，并持一份原生纹理副本
	// （适配器另有一张按名字的强引用表）。本批不提供显式销毁 —— 纹理由适配器保活到进程末。
	class CScriptTexture final : public IScriptTexture
	{
	public:
		CScriptTexture(uint32_t width, uint32_t height, castl::shared_ptr<GPUTexture> nativeTexture);

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

		// 原生测试钩子（C++ only）：底层真实纹理。
		castl::shared_ptr<GPUTexture> const& GetNativeTextureForTest() const { return m_NativeTexture; }

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		castl::shared_ptr<GPUTexture> m_NativeTexture;
	};

	// 目标：窗口后台缓冲或固定格式离屏纹理。保存描述快照，原生测试用它核对宽、高与格式。
	class CScriptRenderTarget final : public IScriptRenderTarget
	{
	public:
		CScriptRenderTarget(ImageHandle image, GPUTextureDescriptor const& descriptor, bool backbuffer);

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;
		bool IsBackbuffer() const override;

		// 原生测试钩子（C++ only，不在任何脚本接口里）。
		ImageHandle const& GetImageForTest() const { return m_Image; }
		GPUTextureDescriptor const& GetDescriptorForTest() const { return m_Descriptor; }

	private:
		ImageHandle m_Image;
		GPUTextureDescriptor m_Descriptor{};
		bool m_Backbuffer = false;
	};

	// 参数结构窄对象（csharp-render-shader-constants 任务 2.1）：把「类型名 + 编译元数据」的写入面
	// 收敛成两个**可校验**方法。全部校验在这里完成，绝不把无效输入交给具体后端 ——
	// `ShaderStruct` 的两个实现（D3D12 / Vulkan）对未知字段静默忽略、对越界下标只 `CA_ASSERT`。
	class CScriptShaderStruct final : public IScriptShaderStruct
	{
	public:
		CScriptShaderStruct(cacore::NameHash const& typeName,
			ShaderCompilerSlang::ShaderStructData const* data,
			castl::shared_ptr<ShaderStruct> shaderStruct, ScriptShaderCallCounters* counters);

		int32_t SetFloat3(cacore::NameHash const& field, float x, float y, float z) override;
		int32_t SetStruct(cacore::NameHash const& field,
			castl::shared_ptr<IScriptShaderStruct> const& sub, uint32_t elementIndex) override;
		// csharp-render-structured-buffer 任务 2.1：字段级校验当场做（缓冲表、读写类型、数组长度），
		// 缓冲名此刻只登记不解析（原生的先绑后建时序）；通过后转发给真实 `ShaderStruct::SetBuffer`。
		int32_t SetBuffer(cacore::NameHash const& field, cacore::NameHash const& bufferName) override;
		// csharp-render-texture-sampling 任务 2.2：纹理与采样器绑定。`SetImage` 校验纹理表条目
		//（存在 / 只读 / 描述符数组长度 1）与非空；`SetSampler` 只校验采样器名字表存在性。
		// 两者都 MUST NOT 把无效输入转交真实 `ShaderStruct`（两后端对未知字段静默跳过、越界 `CA_ASSERT`）。
		int32_t SetImage(cacore::NameHash const& field,
			castl::shared_ptr<IScriptTexture> const& texture) override;
		int32_t SetSampler(cacore::NameHash const& field,
			TextureSamplerDescriptor const& sampler) override;
		castl::string GetLastError() const override;

		// 提交检查的只读数据面（任务 2.2）：字段是否被 `SetBuffer` 写过、写的是哪个缓冲名。
		bool HasBoundBuffer(cacore::NameHash const& field) const;
		cacore::NameHash GetBoundBufferName(cacore::NameHash const& field) const;
		// csharp-render-texture-sampling 任务 2.2：纹理与采样器的只读核对口（D5）——
		// 计数证明不了"解析到谁"，4.1 要按对象同一性核对，所以这里必须交出引用本身。
		bool HasBoundImage(cacore::NameHash const& field) const;
		castl::shared_ptr<IScriptTexture> GetBoundImage(cacore::NameHash const& field) const;
		bool HasBoundSampler(cacore::NameHash const& field) const;
		TextureSamplerDescriptor GetBoundSampler(cacore::NameHash const& field) const;
		// 编译元数据指针（只读；提交检查要按它的 `m_Buffers` 逐字段核对）。
		ShaderCompilerSlang::ShaderStructData const* GetDataForTest() const { return m_Data; }

		// 原生测试钩子（C++ only）：类型名、真实 `ShaderStruct` 与两个写入计数。
		cacore::NameHash const& GetTypeNameForTest() const { return m_TypeName; }
		castl::shared_ptr<ShaderStruct> const& GetShaderStructForTest() const { return m_ShaderStruct; }

	private:
		int32_t Fail(char const* parameter, char const* reason);
		void ClearError() { m_LastError.clear(); }

		cacore::NameHash m_TypeName;
		ShaderCompilerSlang::ShaderStructData const* m_Data = nullptr;
		castl::shared_ptr<ShaderStruct> m_ShaderStruct;
		ScriptShaderCallCounters* m_Counters = nullptr;
		// 字段 → 缓冲名（任务 2.1）：重复绑定覆盖写入，提交解析取最后一次；与流名查重的
		// 「重复即拒绝」语义不同（缓冲绑定是状态写入，有效性由提交检查兜底）。
		castl::unordered_map<cacore::NameHash, cacore::NameHash> m_BoundBuffers;
		// csharp-render-texture-sampling 任务 2.2：字段 → 纹理引用 / 采样器描述。同样覆盖写入，
		// 提交检查按这两张表核对"每个纹理 / 采样器条目都写过"。
		castl::unordered_map<cacore::NameHash, castl::shared_ptr<IScriptTexture>> m_BoundImages;
		castl::unordered_map<cacore::NameHash, TextureSamplerDescriptor> m_BoundSamplers;
		castl::string m_LastError;
	};

	// 图：持有一份真实 `GPUGraph`，以及本图已登记缓冲与顶点流的描述。
	class CScriptRenderGraph final : public IScriptRenderGraph
	{
	public:
		CScriptRenderGraph(CRenderBackend* backend, ScriptShaderCallCounters* counters);

		int32_t AddBuffer(cacore::NameHash const& name, GPUBufferDescriptor const& descriptor,
			castl::vector<uint8_t> const& data) override;
		int32_t SetPositionColorStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t colorOffset) override;
		int32_t SetPositionStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset) override;
		// csharp-render-texture-sampling 任务 2.3：`POSITION float3` + `TEXCOORD float2` 流。
		// `TEXCOORD` 的属性格式是 8 字节的 `eR32G32_SFloat`（不是原生测试那种用 12 字节三分量格式
		// 声明 `float2` 的越界写法），描述符经 `GetGraphForTest()` 回读核对。
		int32_t SetPositionTextureStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t texcoordOffset) override;
		int32_t SetPassParam(uint32_t passIndex, cacore::NameHash const& blockName,
			castl::shared_ptr<IScriptShaderStruct> const& parameters) override;
		int32_t AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target,
			cacore::PathHash const& shaderPath, cacore::NameHash const& streamName,
			cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green,
			float blue, float alpha) override;
		int32_t Present(castl::shared_ptr<IScriptRenderTarget> const& target) override;
		uint32_t GetPassCount() const override;
		castl::string GetLastError() const override;

		// 原生测试钩子：真实图结构（上传字节 / 属性 / 着色器 / 绘制数 / 清除色 / 呈现清单）。
		castl::shared_ptr<GPUGraph> const& GetGraphForTest() const { return m_Graph; }

		// csharp-render-structured-buffer 任务 2.2：提交检查的只读查询与数据通道。
		// `HasBuffer` 查本图缓冲登记表；`GetBoundBufferNameForTest` 读图侧登记表解析出的缓冲名
		//（4.1 核对用，不读后端 `GetBufferHandles` —— 那方法只在两个具体后端类上）；
		// `CheckBoundBuffers` 在提交前逐 pass 核对「字段已写 + 缓冲名在本图登记」。
		bool HasBuffer(cacore::NameHash const& name) const;
		cacore::NameHash GetBoundBufferNameForTest(uint32_t passIndex, cacore::NameHash const& blockName,
			cacore::NameHash const& field) const;
		int32_t CheckBoundBuffers(castl::string& outParameter, castl::string& outReason) const;
		// csharp-render-texture-sampling 任务 2.2：纹理字段的只读核对口。返回的是图侧登记表里
		// 那个参数结构持有的**引用本身**（对象同一性判据用，计数替代不了）。
		castl::shared_ptr<IScriptTexture> GetBoundImageForTest(uint32_t passIndex,
			cacore::NameHash const& blockName, cacore::NameHash const& field) const;
		// 提交前核对「纹理表与采样器表每条目都已写」。与 `CheckBoundBuffers` 分开只是为了错误文本
		// 各对各的参数名，二者都在 `ExecuteGraph` 之前调用。
		int32_t CheckBoundTextures(castl::string& outParameter, castl::string& outReason) const;

	private:
		struct BufferRecord
		{
			GPUBufferDescriptor descriptor{};
		};
		struct StreamRecord
		{
			uint32_t stride = 0;
			uint32_t positionOffset = 0;
			uint32_t colorOffset = 0;
			// csharp-render-texture-sampling 任务 2.3：TEXCOORD 的字节偏移（8 字节属性）。
			uint32_t texcoordOffset = 0;
			cacore::HashObj<VertexInputsDescriptor> vertices;
		};
		// `SetPassParam` 成功登记：`(passIndex, blockName) → 窄参数结构`。强引用（与参数保活同孔）——
		// 脚本释放自己的表面对象之后，提交检查与测试核对仍能读到字段映射。
		struct PassParamRecord
		{
			uint32_t passIndex = 0;
			cacore::NameHash blockName;
			castl::shared_ptr<CScriptShaderStruct> parameters;
		};

		int32_t Fail(char const* parameter, char const* reason);
		void ClearError() { m_LastError.clear(); }

		// 非拥有：只用来查编译元数据（`FindShaderStructData` / `FindShaderRootStructData`）与
		// 创建真实 `ShaderStruct`。后端由宿主持有，生命周期长于本图。
		CRenderBackend* m_Backend = nullptr;
		// 非拥有：计数槽归后端持有，本图只在自己成功绑定参数时加一（任务 3.2 的读数来源）。
		ScriptShaderCallCounters* m_Counters = nullptr;
		castl::shared_ptr<GPUGraph> m_Graph;
		castl::unordered_map<cacore::NameHash, BufferRecord> m_Buffers;
		castl::unordered_map<cacore::NameHash, StreamRecord> m_Streams;
		castl::vector<PassParamRecord> m_PassParams;
		castl::string m_LastError;
	};

	// 后端适配器：非拥有地读取模块管理器里的 `CRenderBackend` 与 `CThreadManager`。
	class CScriptRenderBackend final : public IScriptRenderBackend
	{
	public:
		CScriptRenderBackend(CRenderBackend* backend, thread_management::CThreadManager* threadManager);

		castl::shared_ptr<IScriptRenderTarget> GetWindowTarget(
			castl::shared_ptr<cawindow::IWindow> const& window) override;
		castl::shared_ptr<IScriptRenderTarget> CreateCaptureTarget(uint32_t width,
			uint32_t height) override;
		castl::shared_ptr<IScriptRenderGraph> CreateGraph() override;
		// csharp-render-texture-sampling 任务 2.1：一次调用完成创建 + 上传 + 转采样态 + 同步等待。
		// 上传执行走**后端直连**（`ExecuteGraph` + `WaitIdle`），不经适配器的 `SubmitGraph`/`WaitIdle`：
		// `GetExecuteGraphCountForTest`/`GetWaitIdleCountForTest`/`m_Pending` 只反映脚本提交。
		castl::shared_ptr<IScriptTexture> CreateTexture(cacore::NameHash const& name, uint32_t width,
			uint32_t height, castl::vector<uint8_t> const& rgba) override;
		// 未知类型：返回空句柄 + `GetLastError()` 点名 `structTypeName`，且 MUST NOT 进入后端
		// `CRenderBackend::CreateShaderStruct`（D3D12 侧对空元数据解引用）。非空类型才创建真实
		// `ShaderStruct` 并包进窄对象。
		castl::shared_ptr<IScriptShaderStruct> CreateShaderStruct(
			cacore::NameHash const& structTypeName) override;
		int32_t SubmitGraph(castl::shared_ptr<IScriptRenderGraph> const& graph) override;
		void WaitIdle() override;
		castl::string GetLastError() const override;

		// 原生测试钩子：等待次数、待完成集合大小、最近一次提交的弱图引用。
		uint32_t GetWaitIdleCountForTest() const { return m_WaitIdleCount; }
		uint32_t GetPendingGraphCountForTest() const
		{
			return static_cast<uint32_t>(m_Pending.size());
		}
		castl::weak_ptr<GPUGraph> GetLastSubmittedGraphForTest() const { return m_LastSubmittedGraph; }
		// 最近一次进入 `SubmitGraph` 的图对象（弱引用，不延长生命）：`skip-draw` 的负向判据要在
		// 空图被拒绝之前读到它的 pass 数。只给原生测试用。
		castl::weak_ptr<CScriptRenderGraph> GetLastGraphForTest() const { return m_LastGraph; }
		// 最近一次由脚本创建的离屏 / 窗口目标：测试从它拿真实 `ImageHandle` 与描述快照。
		castl::shared_ptr<CScriptRenderTarget> GetLastCaptureTargetForTest() const
		{
			return m_LastCaptureTarget;
		}
		castl::shared_ptr<CScriptRenderTarget> GetLastWindowTargetForTest() const
		{
			return m_LastWindowTarget;
		}
		// csharp-render-shader-constants 任务 2.1 / 3.2 的三个调用计数：正常探针必须读到
		// 4 次参数结构创建、3 次 `SetFloat3`、3 次 `SetStruct`；非探针测试期间它们保持 0。
		uint32_t GetCreateShaderStructCountForTest() const { return m_ShaderCounters.createShaderStruct; }
		uint32_t GetSetFloat3CountForTest() const { return m_ShaderCounters.setFloat3; }
		uint32_t GetSetStructCountForTest() const { return m_ShaderCounters.setStruct; }
		// csharp-render-structured-buffer 任务 2.1：成功 `SetBuffer` 与成功 `AddBuffer` 的计数。
		uint32_t GetSetBufferCountForTest() const { return m_ShaderCounters.setBuffer; }
		uint32_t GetAddBufferCountForTest() const { return m_ShaderCounters.addBuffer; }
		// 成功绑定参数块的次数（任务 3.2 要求"两次 SetPassParam"也有原生读数）。
		uint32_t GetSetPassParamCountForTest() const { return m_ShaderCounters.setPassParam; }
		// `CreateCaptureTarget` 与进入后端的执行次数：非探针测试期间必须为 0（任务 3.2），
		// `skip-param` 的提交前拒绝也必须证明"后端执行次数 0"（任务 4.1）。
		uint32_t GetCreateCaptureTargetCountForTest() const { return m_CreateCaptureTargetCount; }
		uint32_t GetExecuteGraphCountForTest() const { return m_ExecuteGraphCount; }
		// csharp-render-texture-sampling 任务 2.1：成功创建纹理的次数；"上传执行"单列计数（D24）——
		// 一切"本次提交执行增量 0"的判据只看 `GetExecuteGraphCountForTest`，上传那 1 次不进它。
		uint32_t GetCreateTextureCountForTest() const { return m_ShaderCounters.createTexture; }
		uint32_t GetTextureUploadExecuteCountForTest() const { return m_TextureUploadExecuteCount; }
		// csharp-render-texture-sampling 任务 2.2：成功绑定纹理 / 采样器的次数。
		uint32_t GetSetImageCountForTest() const { return m_ShaderCounters.setImage; }
		uint32_t GetSetSamplerCountForTest() const { return m_ShaderCounters.setSampler; }
		// 最近一次成功创建的纹理（任务 4.1 的对象同一性核对：图侧 `GetBoundImageForTest` 读出的
		// 必须是**这个对象**，计数替代不了同一性）。
		castl::shared_ptr<IScriptTexture> const& GetLastCreatedTextureForTest() const
		{
			return m_LastCreatedTexture;
		}

	private:
		int32_t Fail(char const* parameter, char const* reason);
		void ClearError() { m_LastError.clear(); }

		CRenderBackend* m_Backend = nullptr;
		thread_management::CThreadManager* m_ThreadManager = nullptr;
		// 待完成集合持有图强引用，`WaitIdle` 之后才清空（design 三）。
		castl::vector<castl::shared_ptr<IScriptRenderGraph>> m_Pending;
		uint32_t m_WaitIdleCount = 0;
		ScriptShaderCallCounters m_ShaderCounters;
		uint32_t m_CreateCaptureTargetCount = 0;
		uint32_t m_ExecuteGraphCount = 0;
		castl::weak_ptr<GPUGraph> m_LastSubmittedGraph;
		castl::weak_ptr<CScriptRenderGraph> m_LastGraph;
		castl::shared_ptr<CScriptRenderTarget> m_LastCaptureTarget;
		castl::shared_ptr<CScriptRenderTarget> m_LastWindowTarget;
		// csharp-render-texture-sampling 任务 2.1：纹理强引用表（名字 → 原生纹理）+ 上传执行计数 +
		// 最近创建的窄纹理。表与窄对象各持一份 shared_ptr 副本，适配器销毁前纹理不失效。
		castl::unordered_map<cacore::NameHash, castl::shared_ptr<GPUTexture>> m_Textures;
		uint32_t m_TextureUploadExecuteCount = 0;
		castl::shared_ptr<IScriptTexture> m_LastCreatedTexture;
		castl::string m_LastError;
	};
}
