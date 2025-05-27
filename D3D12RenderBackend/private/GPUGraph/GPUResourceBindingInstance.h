#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include "GPUResourceStates.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
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

	class GPUResourceBindingInstance : public D3D12SubobjectBase
	{
	public:


		struct ImageBindingInfo
		{
			struct ImageBinding
			{
				ImageHandle image;
				GPUTextureView textureView;
			};
			ShaderCompilerSlang::EShaderResourceType resourceType;
			ShaderCompilerSlang::EShaderResourceAccess accessType;
			bool isUAV() const
			{
				return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
					(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
			}
			uint32_t bindingID;
			std::vector<ImageBinding> bindings;
		};

		struct BufferBindingInfo
		{
			ShaderCompilerSlang::EShaderResourceType resourceType;
			ShaderCompilerSlang::EShaderResourceAccess accessType;
			bool isUAV() const
			{
				return (resourceType == ShaderCompilerSlang::EShaderResourceType::eRWTexture) ||
					(resourceType == ShaderCompilerSlang::EShaderResourceType::eRWStructuredBuffer);
			}
			uint32_t bindingID;
			std::vector<BufferHandle> bindings;
		};

		struct CBufferBindingInfo
		{
			uint32_t bindingID;
			D3D2ShaderStruct const* pCBufferStruct;
			BufferHandle cbufferHandle;
		};

		struct SamplerBindingInfo
		{
			uint32_t bindingID;
			castl::vector<TextureSamplerDescriptor> samplerDescriptors;
		};

		struct GPUResourceSpaceInfo
		{
			uint32_t spaceID;
			DescriptorAllocation descriptorAllocation;
			DescriptorAllocation samplerAllocation;
			std::vector<D3D12_DESCRIPTOR_RANGE1> descTable;
			castl::vector<CBufferBindingInfo> cbufferInfos;
			castl::vector<ImageBindingInfo> imageInfo;
			castl::vector<BufferBindingInfo> bufferInfos;
			castl::vector<SamplerBindingInfo> samplerInfos;
		};
	public:
		GPUResourceBindingInstance(RenderBackend_D3D12* app) : D3D12SubobjectBase(app) {}
		void Init(ShaderResourceSet const& resourceSet
			, GPUConstantBufferManager& cbufferManager);
		void Init1(ShaderResourceSet const& resourceSet
			, GPUConstantBufferManager& cbufferManager);
		void BuildResources(GPUGraph const& gpuGraph, D3D12GraphLocalResourceManager& resourceManager);
		void IterateResourceUsages(castl::function<void(ImageBindingInfo const&)>const& imageCallback,
		castl::function<void(BufferBindingInfo const&)>const& bufferCallback) const;
		void BuildDescriptors(D3D12GraphLocalResourceManager& resourceManager
			, GPUDescriptorHeap& gpuDescriptorHeap
			, GPUDescriptorHeap& samplerDescriptorHeap);
	private:
		castl::vector<GPUResourceSpaceInfo> m_GPUResourceSpaceInfos;
		ComPtr<ID3D12RootSignature> m_RootSignature;
		ShaderInfo shaderInfo;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
	};

	using ShaderResourceInstanceDic = castl::shared_dic<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>>;
}