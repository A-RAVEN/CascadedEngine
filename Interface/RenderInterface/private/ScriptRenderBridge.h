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

	// 图：持有一份真实 `GPUGraph`，以及本图已登记缓冲与顶点流的描述。
	class CScriptRenderGraph final : public IScriptRenderGraph
	{
	public:
		CScriptRenderGraph();

		int32_t AddBuffer(cacore::NameHash const& name, GPUBufferDescriptor const& descriptor,
			castl::vector<uint8_t> const& data) override;
		int32_t SetPositionColorStream(cacore::NameHash const& streamName, uint32_t stride,
			uint32_t positionOffset, uint32_t colorOffset) override;
		int32_t AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target,
			cacore::PathHash const& shaderPath, cacore::NameHash const& streamName,
			cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green,
			float blue, float alpha) override;
		int32_t Present(castl::shared_ptr<IScriptRenderTarget> const& target) override;
		uint32_t GetPassCount() const override;
		castl::string GetLastError() const override;

		// 原生测试钩子：真实图结构（上传字节 / 属性 / 着色器 / 绘制数 / 清除色 / 呈现清单）。
		castl::shared_ptr<GPUGraph> const& GetGraphForTest() const { return m_Graph; }

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
			cacore::HashObj<VertexInputsDescriptor> vertices;
		};

		int32_t Fail(char const* parameter, char const* reason);
		void ClearError() { m_LastError.clear(); }

		castl::shared_ptr<GPUGraph> m_Graph;
		castl::unordered_map<cacore::NameHash, BufferRecord> m_Buffers;
		castl::unordered_map<cacore::NameHash, StreamRecord> m_Streams;
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

	private:
		int32_t Fail(char const* parameter, char const* reason);
		void ClearError() { m_LastError.clear(); }

		CRenderBackend* m_Backend = nullptr;
		thread_management::CThreadManager* m_ThreadManager = nullptr;
		// 待完成集合持有图强引用，`WaitIdle` 之后才清空（design 三）。
		castl::vector<castl::shared_ptr<IScriptRenderGraph>> m_Pending;
		uint32_t m_WaitIdleCount = 0;
		castl::weak_ptr<GPUGraph> m_LastSubmittedGraph;
		castl::weak_ptr<CScriptRenderGraph> m_LastGraph;
		castl::shared_ptr<CScriptRenderTarget> m_LastCaptureTarget;
		castl::shared_ptr<CScriptRenderTarget> m_LastWindowTarget;
		castl::string m_LastError;
	};
}
