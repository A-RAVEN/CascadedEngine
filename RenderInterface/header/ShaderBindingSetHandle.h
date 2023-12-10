#pragma once
#include "ShaderBindingSet.h"
#include "GPUBufferHandle.h"
#include <unordered_map>

namespace graphics_backend
{
	class CRenderGraph;
	class IShaderBindingSetData
	{
	public:
		virtual void SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet) = 0;
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
		virtual std::unordered_map<std::string, TextureHandle> const& GetInternalTextures() const = 0;
		virtual std::unordered_map<std::string, GPUBufferHandle> const& GetInternalGPUBuffers() const = 0;
	};

	class ShaderBindingSetHandle
	{
	public:
		ShaderBindingSetHandle() = default;
		ShaderBindingSetHandle(CRenderGraph* renderGraph, TIndex descIndex, TIndex setIndex)
			: m_DescIndex(descIndex)
			, m_SetIndex(setIndex)
			, p_RenderGraph(renderGraph) {}
		inline ShaderBindingSetHandle& SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet);
		inline ShaderBindingSetHandle& SetTexture(std::string const& name
			, std::shared_ptr<GPUTexture> const& pTexture);
		inline ShaderBindingSetHandle& SetTexture(std::string const& name
			, TextureHandle const& textureHandle);
		inline ShaderBindingSetHandle& SetSampler(std::string const& name
			, std::shared_ptr<TextureSampler> const& pSampler);
		inline TIndex GetBindingSetIndex() const {
			return m_SetIndex;
		}
		inline TIndex GetBindingSetDescIndex() const {
			return m_DescIndex;
		}
	private:
		CRenderGraph* p_RenderGraph = nullptr;
		TIndex m_DescIndex = INVALID_INDEX;
		TIndex m_SetIndex = INVALID_INDEX;
	};

	class ShaderBindingList
	{
	public:
		std::vector<std::shared_ptr<ShaderBindingSet>> m_ShaderBindingSets;
		std::vector<ShaderBindingSetHandle> m_BindingSetHandles;
	};
}