#pragma once
#include "ShaderBindingSet.h"
#include "GPUBufferHandle.h"
#include "GPUGraphHandleBase.h"
#include <unordered_map>

namespace graphics_backend
{
	class CRenderGraph;

	class IShaderConstantSetData
	{
	public:
		virtual TIndex GetDescID() const = 0;
		virtual void SetValue(std::string const& name, void* pValue) = 0;
		virtual void const* GetUploadingDataPtr() const = 0;
		virtual uint32_t GetUploadingDataByteSize() const = 0;
		virtual void PopulateCachedData(std::vector<std::tuple<std::string, uint32_t, uint32_t>>& outData) const = 0;
	};


	class ShaderConstantSetHandle : public GPUGraphHandleBase
	{
	public: 
		ShaderConstantSetHandle() = default;
		ShaderConstantSetHandle(CRenderGraph* renderGraph, TIndex handleID)
			: GPUGraphHandleBase(renderGraph, handleID)
		{}
		inline ShaderConstantSetHandle& SetValue(std::string const& name, void* pValue);
		template<typename T>
		ShaderConstantSetHandle SetValue(std::string const& name, T const& value)
		{
			return SetValue(name, (void*)&value);
		}
	};


	class IShaderBindingSetData
	{
	public:
		virtual void SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet) = 0;
		virtual void SetConstantSet(std::string const& name, ShaderConstantSetHandle const& constantSetHandle) = 0;
		virtual void SetTexture(std::string const& name
			, std::shared_ptr<GPUTexture> const& pTexture) = 0;
		virtual void SetTexture(std::string const& name
			, TextureHandle const& textureHandle) = 0;
		virtual void SetGPUBuffer(std::string const& name
			, GPUBufferHandle const& bufferHandle) = 0;
		virtual void SetSampler(std::string const& name
			, std::shared_ptr<TextureSampler> const& pSampler) = 0;
		virtual uint32_t GetBindingSetDescIndex() const = 0;

		virtual std::unordered_map<std::string, std::shared_ptr<ShaderConstantSet>> const& GetExternalConstantSets() const = 0;
		virtual std::unordered_map<std::string, std::shared_ptr<GPUTexture>> const& GetExternalTextures() const = 0;
		virtual std::unordered_map<std::string, std::shared_ptr<TextureSampler>> const& GetExternalSamplers() const = 0;
		virtual std::unordered_map<std::string, ShaderConstantSetHandle> const& GetInternalConstantSets() const = 0;
		virtual std::unordered_map<std::string, TextureHandle> const& GetInternalTextures() const = 0;
		virtual std::unordered_map<std::string, GPUBufferHandle> const& GetInternalGPUBuffers() const = 0;
	};

	class ShaderBindingSetHandle : public GPUGraphHandleBase
	{
	public:
		ShaderBindingSetHandle() = default;
		ShaderBindingSetHandle(CRenderGraph* renderGraph, TIndex descIndex, TIndex handleID)
			: GPUGraphHandleBase(renderGraph, handleID)
		{}
		inline ShaderBindingSetHandle& SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet);
		inline ShaderBindingSetHandle& SetConstantSet(std::string const& name, ShaderConstantSetHandle const& constantSetHandle);
		inline ShaderBindingSetHandle& SetTexture(std::string const& name
			, std::shared_ptr<GPUTexture> const& pTexture);
		inline ShaderBindingSetHandle& SetTexture(std::string const& name
			, TextureHandle const& textureHandle);
		inline ShaderBindingSetHandle& SetSampler(std::string const& name
			, std::shared_ptr<TextureSampler> const& pSampler);
	};

	class ShaderBindingList
	{
	public:
		std::vector<std::shared_ptr<ShaderBindingSet>> m_ShaderBindingSets;
		std::vector<ShaderBindingSetHandle> m_BindingSetHandles;
	};
}