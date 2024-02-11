#pragma once
#include <CASTL/CAUnorderedMap.h>
#include "ShaderBindingSet.h"
#include "TextureHandle.h"
#include "GPUBufferHandle.h"
#include "GPUGraphHandleBase.h"

namespace graphics_backend
{
	class CRenderGraph;

	class IShaderConstantSetData
	{
	public:
		virtual TIndex GetDescID() const = 0;
		virtual void SetValue(castl::string const& name, void* pValue) = 0;
		virtual void const* GetUploadingDataPtr() const = 0;
		virtual uint32_t GetUploadingDataByteSize() const = 0;
		virtual void PopulateCachedData(castl::vector<castl::tuple<castl::string, uint32_t, uint32_t>>& outData) const = 0;
	};


	class ShaderConstantSetHandle : public GPUGraphHandleBase
	{
	public: 
		ShaderConstantSetHandle() = default;
		ShaderConstantSetHandle(CRenderGraph* renderGraph, TIndex handleID)
			: GPUGraphHandleBase(renderGraph, handleID)
		{}
		inline ShaderConstantSetHandle& SetValue(castl::string const& name, void* pValue);
		template<typename T>
		ShaderConstantSetHandle SetValue(castl::string const& name, T const& value)
		{
			return SetValue(name, (void*)&value);
		}
	};


	class IShaderBindingSetData
	{
	public:
		virtual void SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet) = 0;
		virtual void SetConstantSet(castl::string const& name, ShaderConstantSetHandle const& constantSetHandle) = 0;
		virtual void SetTexture(castl::string const& name
			, castl::shared_ptr<GPUTexture> const& pTexture) = 0;
		virtual void SetTexture(castl::string const& name
			, TextureHandle const& textureHandle) = 0;
		virtual void SetGPUBuffer(castl::string const& name
			, GPUBufferHandle const& bufferHandle) = 0;
		virtual void SetSampler(castl::string const& name
			, castl::shared_ptr<TextureSampler> const& pSampler) = 0;
		virtual uint32_t GetBindingSetDescIndex() const = 0;

		virtual castl::unordered_map<castl::string, castl::shared_ptr<ShaderConstantSet>> const& GetExternalConstantSets() const = 0;
		virtual castl::unordered_map<castl::string, castl::shared_ptr<GPUTexture>> const& GetExternalTextures() const = 0;
		virtual castl::unordered_map<castl::string, castl::shared_ptr<TextureSampler>> const& GetExternalSamplers() const = 0;
		virtual castl::unordered_map<castl::string, ShaderConstantSetHandle> const& GetInternalConstantSets() const = 0;
		virtual castl::unordered_map<castl::string, TextureHandle> const& GetInternalTextures() const = 0;
		virtual castl::unordered_map<castl::string, GPUBufferHandle> const& GetInternalGPUBuffers() const = 0;
	};

	class ShaderBindingSetHandle : public GPUGraphHandleBase
	{
	public:
		ShaderBindingSetHandle() = default;
		ShaderBindingSetHandle(CRenderGraph* renderGraph, TIndex descIndex, TIndex handleID)
			: GPUGraphHandleBase(renderGraph, handleID)
		{}
		inline ShaderBindingSetHandle& SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet);
		inline ShaderBindingSetHandle& SetConstantSet(castl::string const& name, ShaderConstantSetHandle const& constantSetHandle);
		inline ShaderBindingSetHandle& SetTexture(castl::string const& name
			, castl::shared_ptr<GPUTexture> const& pTexture);
		inline ShaderBindingSetHandle& SetTexture(castl::string const& name
			, TextureHandle const& textureHandle);
		inline ShaderBindingSetHandle& SetSampler(castl::string const& name
			, castl::shared_ptr<TextureSampler> const& pSampler);
	};

	class ShaderBindingList
	{
	public:
		castl::vector<castl::shared_ptr<ShaderBindingSet>> m_ShaderBindingSets;
		castl::vector<ShaderBindingSetHandle> m_BindingSetHandles;
	};
}