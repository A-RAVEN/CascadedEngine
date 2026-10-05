#pragma once
#include <Utils/D3D12SubobjectBase.h>
#include <GPUGraph.h>
#include <CASTL/CAArrayRef.h>
#include <ResourceManagment/GPUResourceStates.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <CACore/CASharedDic.h>
#include <ShaderLibrary/ShaderLibrary.h>
#include <CodeGenHash.h>   // 末尾那份手写哈希特化要用 `cacore::codegen::hasher` 的本体
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

// `ShaderResourceSet` 是哈希键（`shared_dic<ShaderResourceSet, …>`），但它**不能进生成表**：`resourceDic`
// 是"名字 → 结构体指针"的映射，指针（哪怕只是容器的元素）是工具明令拒绝的形状。按设计给的第二条路
// 手写一份哈希特化：算的是 shader 信息与**资源名集合**，指针地址不进哈希 —— 地址是进程内的东西，进了
// 哈希只会让同一个资源集在不同进程算出不同的桶。名字的顺序不影响结果（每个名字单独算一遍再异或），
// 所以"插入顺序不同"不会改变哈希，与生成代码对无序容器的口径一致。判等仍走 `operator<=>`（含指针），
// 所以哈希相同不会把两个不同的资源集判成同一个。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<graphics_backend::ShaderResourceSet>
	{
		static void hash(graphics_backend::ShaderResourceSet const& obj, auto& hasher)
		{
			hasher.hash(obj.shaderInfo);
			hasher.hash(static_cast<std::uint64_t>(obj.resourceDic.size()));
			std::uint64_t nameHashXor = 0;
			for (auto const& entry : obj.resourceDic)
			{
				::cacore::codegen::hasher<> nameHasher;   // 参数名就叫 hasher，这里必须写全限定名
				nameHasher.hash(entry.first);
				nameHashXor ^= static_cast<std::uint64_t>(nameHasher.getHash());
			}
			hasher.hash_raw(&nameHashXor, sizeof(nameHashXor));
		}
	};
}