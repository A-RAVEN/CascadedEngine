#pragma once
#include <CRenderGraph.h>

namespace graphics_backend
{
	class ShaderBindingSetData_Internal : public IShaderBindingSetData
	{
	public:
		ShaderBindingSetData_Internal() = default;
		ShaderBindingSetData_Internal(TIndex descIndex) : m_DescriptorIndex(descIndex) {}
		virtual void SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet) override;
		virtual void SetConstantSet(castl::string const& name, ShaderConstantSetHandle const& constantSetHandle) override;
		virtual void SetTexture(castl::string const& name
			, castl::shared_ptr<GPUTexture> const& pTexture) override;
		virtual void SetTexture(castl::string const& name
			, TextureHandle const& textureHandle) override;
		virtual void SetGPUBuffer(castl::string const& name
			, GPUBufferHandle const& bufferHandle) override;
		virtual void SetSampler(castl::string const& name
			, castl::shared_ptr<TextureSampler> const& pSampler) override;
		virtual uint32_t GetBindingSetDescIndex() const override {
			return m_DescriptorIndex;
		}

		virtual castl::unordered_map<castl::string, castl::shared_ptr<ShaderConstantSet>> const& GetExternalConstantSets() const override {
			return m_ExternalConstantSets;
		}

		virtual castl::unordered_map<castl::string, castl::shared_ptr<GPUTexture>> const& GetExternalTextures() const override {
			return m_ExternalTextures;
		}

		virtual castl::unordered_map<castl::string, castl::shared_ptr<TextureSampler>> const& GetExternalSamplers() const override {
			return m_ExternalSamplers;
		}

		virtual castl::unordered_map<castl::string, ShaderConstantSetHandle> const& GetInternalConstantSets() const override
		{
			return m_InternalConstantSets;
		}

		virtual castl::unordered_map<castl::string, TextureHandle> const& GetInternalTextures() const override {
			return m_InternalTextures;
		}

		virtual castl::unordered_map<castl::string, GPUBufferHandle> const& GetInternalGPUBuffers() const override {
			return m_InternalBuffers;
		}
	private:
		TIndex m_DescriptorIndex = INVALID_INDEX;
		castl::unordered_map<castl::string, castl::shared_ptr<ShaderConstantSet>> m_ExternalConstantSets;
		castl::unordered_map<castl::string, castl::shared_ptr<GPUTexture>> m_ExternalTextures;
		castl::unordered_map<castl::string, castl::shared_ptr<TextureSampler>> m_ExternalSamplers;
		castl::unordered_map<castl::string, ShaderConstantSetHandle> m_InternalConstantSets;
		castl::unordered_map<castl::string, TextureHandle> m_InternalTextures;
		castl::unordered_map<castl::string, GPUBufferHandle> m_InternalBuffers;
	};
}