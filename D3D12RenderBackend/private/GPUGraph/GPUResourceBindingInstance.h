#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include <ShaderLibrary/ShaderLibrary.h>
/// <summary>
/// Shader Resource Binding Instance With 
/// </summary>
namespace graphics_backend
{
	class GPUConstantBufferManager : public D3D12SubobjectBase
	{
	public:
		GPUConstantBufferManager(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		BufferHandle GetConstantBufferHandle(D3D2ShaderStruct const* pShaderStruct);
		void BuildResources(D3D12GraphLocalResourceManager& resourceManager);
		void IterateResources(castl::function<void(D3D2ShaderStruct const*, BufferHandle const&)> callback);
		void Clear();
	private:
		castl::shared_dic<D3D2ShaderStruct const*, BufferHandle> m_ConstantBufferHandles;
	};


	struct ShaderResourceSet
	{
		void Init(RenderBackend_D3D12* app, ShaderInfo const& shaderInfo, castl::array_ref<ShaderStructDic const*> const& shaderStructs);
		ShaderInfo shaderInfo;
		castl::unordered_map<cacore::NameHash, D3D2ShaderStruct const*> resourceDic;
		auto operator<=>(ShaderResourceSet const& other) const = default;
	};
	static_assert(cacore::equal_test<ShaderResourceSet>, "ShaderResourceSet  Cannot Equal");
	static_assert(cacore::hashable<ShaderResourceSet>, "Not Hashable");

	class GPUResourceBindingInstance : public D3D12SubobjectBase
	{
	public:

		struct ImageBindingElement
		{
			struct ImageBinding
			{
				ImageHandle image;
				GPUTextureView textureView;
			};
			uint32_t offset;
			ImageBindingInfo bindingInfo;
			std::vector<ImageBinding> bindings;
			EShaderTypeFlags usingStages;
			EResourceUsageFlags resourceUsages;
		};

		struct BufferBindingElement
		{
			uint32_t offset;
			BufferBindingInfo bindingInfo;
			std::vector<BufferHandle> bindings;
			EShaderTypeFlags usingStages;
			EResourceUsageFlags resourceUsages;
		};

		struct CBufferBindingElement
		{
			uint32_t offset;
			CBufferBindingInfo bindingInfo;
			D3D2ShaderStruct const* pCBufferStruct;
			//BufferHandle cbufferHandle;
			EShaderTypeFlags usingStages;
		};

		struct SamplerBindingElement
		{
			uint32_t offset;
			SamplerBindingInfo bindingInfo;
			castl::vector<TextureSamplerDescriptor> samplerDescriptors;
			EShaderTypeFlags usingStages;
		};

		struct GPUResourceBindingInfos
		{
			DescriptorAllocation descriptorAllocation;
			DescriptorAllocation samplerAllocation;
			castl::vector<CBufferBindingElement> cbufferBindings;
			castl::vector<ImageBindingElement> imageBindings;
			castl::vector<BufferBindingElement> bufferBindings;
			castl::vector<SamplerBindingElement> samplerBindings;
			void Release()
			{
				descriptorAllocation.Release();
				samplerAllocation.Release();
				cbufferBindings.clear();
				imageBindings.clear();
				bufferBindings.clear();
				samplerBindings.clear();
			}
		};
	public:
		GPUResourceBindingInstance(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void Init(ShaderResourceSet const& resourceSet);
		void Release();
		void BuildResources(GPUGraph const& gpuGraph, D3D12GraphLocalResourceManager& resourceManager
			, GPUConstantBufferManager& cbufferManager);
		void IterateResourceUsages(castl::function<void(ImageBindingElement const&)>const& imageCallback
			, castl::function<void(BufferBindingElement const&)>const& bufferCallback
			, castl::function<void(CBufferBindingElement const&)>const& cbufferCallback) const;
		void BuildDescriptors(D3D12GraphLocalResourceManager& resourceManager
			, CPUDescriptorAllocatorSet& descriptorAllocatorsr
			, GPUDescriptorHeap& gpuDescriptorHeap
			, GPUDescriptorHeap& samplerDescriptorHeap
			, GPUConstantBufferManager& cbufferManager);
		GPUResourceBindingInfos const& GetBindingInfo() const
		{
			return m_GPUResourceBindingInfos;
		}
	private:
		GPUResourceBindingInfos m_GPUResourceBindingInfos;
		ShaderFileInfo const* pShaderFileInfo = nullptr;
	};

	using ShaderResourceInstanceDic = castl::shared_dic<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>>;

	class ShaderResourceInstanceManager : public D3D12SubobjectBase
	{
	public:
		ShaderResourceInstanceManager(RenderBackend_D3D12* app);
		castl::shared_ptr<GPUResourceBindingInstance> EnsureResourceBindingInstance(ShaderResourceSet const& resourecSet);
		void Release() override
		{
			m_ResourceInstances.clear([](auto& key, auto& instance)
			{
				instance->Release();
			});
		}
	private:
		ShaderResourceInstanceDic m_ResourceInstances;
	};

}