#pragma once
#include <ThreadManager.h>
#include <CRenderGraph.h>
#include <ShaderBindingBuilder.h>
#include <CASTL/CAUnorderedMap.h>
#include "WindowContext.h"
#include "CVulkanThreadContext.h"
#include "FrameCountContext.h"
#include "CVulkanMemoryManager.h"
#include "Containers.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"
#include "VulkanPipelineObject.h"
#include "FramebufferObject.h"
#include "GPUBuffer_Impl.h"

#include "IUploadingResource.h"
#include "GPUObjectManager.h"
#include "RenderGraphExecutor.h"
#include "ShaderBindingSet_Impl.h"
#include "GPUTexture_Impl.h"

namespace graphics_backend
{
	using namespace thread_management;
	class CVulkanApplication
	{
	public:
		CVulkanApplication();
		~CVulkanApplication();
		void InitApp(castl::string const& appName, castl::string const& engineName);
		void InitializeThreadContext(uint32_t threadCount);
		void ReleaseApp();
		void DeviceWaitIdle();
		inline vk::Instance const& GetInstance() const
		{
			return m_Instance;
		}
		inline vk::Device const& GetDevice() const
		{
			return m_Device;
		}
		inline vk::PhysicalDevice const& GetPhysicalDevice() const
		{
			return m_PhysicalDevice;
		}
		inline CVulkanThreadContext* GetThreadContext(uint32_t threadKey) const {
			if (threadKey >= m_ThreadContexts.size())
			{
				return nullptr;
			}
			return &m_ThreadContexts[threadKey];
		}
		GPUObjectManager& GetGPUObjectManager() { return m_GPUObjectManager; }
		CVulkanMemoryManager& GetMemoryManager();

		CVulkanThreadContext& AquireThreadContext();
		void ReturnThreadContext(CVulkanThreadContext& returningContext);
		castl::shared_ptr<CVulkanThreadContext> AquireThreadContextPtr();

		//CTaskGraph* GetGraphicsTaskGraph() const { return p_TaskGraph; }

		//CTask* NewTask();
		//TaskParallelFor* NewTaskParallelFor();
		//CTask* NewUploadingTask(UploadingResourceType resourceType);
		bool AnyWindowRunning() const { return !m_WindowContexts.empty(); }
		castl::shared_ptr<WindowHandle> CreateWindowContext(castl::string windowName, uint32_t initialWidth, uint32_t initialHeight);
		void TickWindowContexts(FrameType currentFrameID);

		CFrameCountContext const& GetSubmitCounterContext() const { return m_SubmitCounterContext; }

		template<typename T, typename...TArgs>
		T SubObject(TArgs&&...Args) const {
			static_assert(castl::is_base_of<ApplicationSubobjectBase, T>::value, "Type T not derived from ApplicationSubobjectBase");
			T newSubObject( castl::forward<TArgs>(Args)... );
			newSubObject.Initialize(this);
			return newSubObject;
		}

		template<typename T, typename...TArgs>
		T& SubObject_EmplaceBack(castl::vector<T>& container,TArgs&&...Args) const {
			static_assert(castl::is_base_of<ApplicationSubobjectBase, T>::value, "Type T not derived from ApplicationSubobjectBase");
			container.emplace_back(castl::forward<TArgs>(Args)...);
			T& newSubObject = container.back();
			newSubObject.Initialize(this);
			return newSubObject;
		}

		template<typename T, typename...TArgs>
		castl::shared_ptr<T> SubObject_Shared(TArgs&&...Args) const {
			static_assert(castl::is_base_of<ApplicationSubobjectBase, T>::value, "Type T not derived from ApplicationSubobjectBase");
			castl::shared_ptr<T> newSubObject = castl::shared_ptr<T>(new T(castl::forward<TArgs>(Args)...), ApplicationSubobjectBase_Deleter{});
			newSubObject->Initialize(this);
			return newSubObject;
		}

		template<typename T, typename...TArgs>
		T NewObject(TArgs&&...Args) const {
			static_assert(castl::is_base_of<VKAppSubObjectBaseNoCopy, T>::value, "Type T not derived from VKAppSubObjectBaseNoCopy");
			T newObject(*this);
			newObject.Initialize(castl::forward<TArgs>(Args)...);
			return newObject;
		}

		void ReleaseSubObject(ApplicationSubobjectBase& subobject) const
		{
			subobject.Release();
		}

		void SyncPresentationFrame(FrameType frameID);
		void ExecuteStates(CTaskGraph* rootTaskGraph, castl::vector<castl::shared_ptr<CRenderGraph>> const& pendingRenderGraphs, FrameType frameID);

		void PushRenderGraph(castl::shared_ptr<CRenderGraph> inRenderGraph);

		void CreateImageViews2D(vk::Format format, castl::vector<vk::Image> const& inImages, castl::vector<vk::ImageView>& outImageViews) const;
		vk::ImageView CreateDefaultImageView(
			GPUTextureDescriptor const& inDescriptor
			, vk::Image inImage
			, bool depthAspect
			, bool stencilAspect) const;
	public:
		//Allocation
		GPUBuffer* NewGPUBuffer(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride);
		void ReleaseGPUBuffer(GPUBuffer* releaseGPUBuffer);

		GPUTexture* NewGPUTexture(GPUTextureDescriptor const& inDescriptor);
		void ReleaseGPUTexture(GPUTexture* releaseGPUTexture);

		castl::shared_ptr<ShaderConstantSet> NewShaderConstantSet(ShaderConstantsBuilder const& builder);
		castl::shared_ptr<ShaderBindingSet> NewShaderBindingSet(ShaderBindingBuilder const& builder);
		castl::shared_ptr<TextureSampler> GetOrCreateTextureSampler(TextureSamplerDescriptor const& descriptor);

		HashPool<ShaderBindingBuilder, ShaderBindingSetAllocator>& GetShaderBindingSetAllocators() { return m_ShaderBindingSetAllocator; }
		HashPool<ShaderConstantsBuilder, ShaderConstantSetAllocator>& GetShaderConstantSetAllocators() { return m_ConstantSetAllocator; }
	private:

		void InitializeInstance(castl::string const& name, castl::string const& engineName);
		void DestroyInstance();
		void EnumeratePhysicalDevices();
		void CreateDevice();
		void DestroyDevice();

		void DestroyThreadContexts();

		void ReleaseAllWindowContexts();


	private:
		vk::Instance m_Instance = nullptr;
		vk::PhysicalDevice m_PhysicalDevice = nullptr;
		vk::Device m_Device = nullptr;
	#if !defined(NDEBUG)
		vk::DebugUtilsMessengerEXT m_DebugMessager = nullptr;
	#endif

		CFrameCountContext m_SubmitCounterContext;
		castl::vector<castl::shared_ptr<CWindowContext>> m_WindowContexts;

		Internal_InterlockedQueue<uint32_t> m_AvailableThreadQueue;
		mutable castl::vector<CVulkanThreadContext> m_ThreadContexts;

		TTickingUpdateResourcePool<GPUBuffer_Impl> m_GPUBufferPool;
		TTickingUpdateResourcePool<GPUTexture_Impl> m_GPUTexturePool;
		//Uniform Buffer
		HashPool<ShaderConstantsBuilder, ShaderConstantSetAllocator> m_ConstantSetAllocator;
		//Shader Descriptor Set
		HashPool<ShaderBindingBuilder, ShaderBindingSetAllocator> m_ShaderBindingSetAllocator;

		GPUObjectManager m_GPUObjectManager;
		RenderGraphExecutorDic m_RenderGraphDic;

		CVulkanMemoryManager m_MemoryManager;
	};
}
