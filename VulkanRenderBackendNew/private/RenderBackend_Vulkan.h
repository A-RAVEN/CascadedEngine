#pragma once
#include <Utils/TypeTraits.h>
#include <CRenderBackend.h>
#include <CACore/CAModuleManager.h>
#include <Utils/VulkanIncludes.h>
#include <Utils/VulkanSubobjectBase.h>
#include <VulkanQueue/QueueContext.h>
#include <VulkanObjectManaging/DescriptorSetLayoutManager.h>
#include <VulkanObjectManaging/PipelineLayoutManager.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <ResourceManagement/VulkanCommandListManager.h>
#include <GPUGraph/VulkanGraphExecutor.h>
#include <PipelineLibrary/VulkanPipelineLibrary.h>
#include <PipelineLibrary/PipelineLibraryCache.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>


namespace graphics_backend
{
	class RenderBackend_Vulkan : public CRenderBackend
	{
	public:
		RenderBackend_Vulkan() = default;
		void Init(cacore::IModuleManager* pModuleManager);
		virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) override;
		virtual void Release() override;
		virtual castl::shared_ptr<GPUBuffer> CreateGPUBuffer(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags) override;
		virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType) override;
		virtual castl::shared_ptr<ShaderStruct> CreateShaderStruct(cacore::NameHash const& structType) override;
		virtual castl::shared_ptr<WindowHandle> GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window) override;
		virtual bool AnyWindowRunning() override;

		vk::Instance const& GetVulkanInstance() const
		{
			return m_VulkanInstance;
		}

		vk::Device const& GetVulkanDevice() const
		{
			return m_Device;
		}

		vk::PhysicalDevice const& GetVulkanPhysicalDevice() const
		{
			return m_PhysicalDevice;
		}

		QueueContext const& GetQueueContext() const
		{
			return m_QueueContext;
		}

		DescriptorSetLayoutContainer& GetDescriptorSetLayoutContainer()
		{
			return m_DescriptorSetLayoutContainer;
		}

		DescriptorSetLayoutContainer const& GetDescriptorSetLayoutContainer() const
		{
			return m_DescriptorSetLayoutContainer;
		}

		PipelineLayoutContainer& GetPipelineLayoutContainer()
		{
			return m_PipelineLayoutContainer;
		}

		PipelineLayoutContainer const& GetPipelineLayoutContainer() const
		{
			return m_PipelineLayoutContainer;
		}

		template<typename T, typename...TArgs>
		void InitSubObj(T* inoutObj, TArgs&...Args)
		{
			static_assert(CanInit<T, TArgs...> && DerivedFrom<T, VulkanSubobjectBase>, "Type T Not Initializable");
			static_cast<VulkanSubobjectBase*>(inoutObj)->SetApp(this);
			inoutObj->Init(castl::forward<TArgs>(Args)...);
		}

		template<typename T>
		void InitSubObj(T* inoutObj)
		{
			static_assert(DerivedFrom<T, VulkanSubobjectBase>, "Type T Not Initializable");
			if constexpr (DerivedFrom<T, VulkanSubobjectBase>)
			{
				static_cast<VulkanSubobjectBase*>(inoutObj)->SetApp(this);
			}
		}

		VulkanMemoryManager& GetMemoryManager() { return m_MemoryManager; }
		VulkanMemoryManager const& GetMemoryManager() const { return m_MemoryManager; }
		VulkanCommandListManager& GetCommandListManager() { return m_CommandListManager; }
		VulkanCommandListManager const& GetCommandListManager() const { return m_CommandListManager; }

		VulkanPipelineLibrary& GetPipelineLibrary() { return m_PipelineLibrary; }
		VulkanPipelineLibrary const& GetPipelineLibrary() const { return m_PipelineLibrary; }
		PipelineLibraryCache& GetPipelineLibraryCache() { return m_PipelineLibraryCache; }
		PipelineLibraryCache const& GetPipelineLibraryCache() const { return m_PipelineLibraryCache; }

		bool IsPipelineLibrarySupported() const { return m_PipelineLibrarySupported; }

	private:

		vk::Instance m_VulkanInstance;
		vk::DebugUtilsMessengerEXT m_DebugMessenger;
		vk::PhysicalDevice m_PhysicalDevice;
		vk::Device m_Device;

		QueueContext m_QueueContext;
		DescriptorSetLayoutContainer m_DescriptorSetLayoutContainer;
		PipelineLayoutContainer m_PipelineLayoutContainer;
		VulkanMemoryManager m_MemoryManager;
		VulkanCommandListManager m_CommandListManager;
		VulkanPipelineLibrary m_PipelineLibrary;
		PipelineLibraryCache m_PipelineLibraryCache;

		bool m_PipelineLibrarySupported = false;

		// Window handle tracking
		castl::unordered_map<cawindow::IWindow*, castl::weak_ptr<WindowHandle>> m_WindowHandles;
	};

}