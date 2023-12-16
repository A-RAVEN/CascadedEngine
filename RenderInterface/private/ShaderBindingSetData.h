#pragma once
#include <header/CRenderGraph.h>

namespace graphics_backend
{
	class ShaderBindingSetData_Internal : public IShaderBindingSetData
	{
	public:
		ShaderBindingSetData_Internal() = default;
		ShaderBindingSetData_Internal(TIndex descIndex) : m_DescriptorIndex(descIndex) {}
		virtual void SetConstantSet(std::string const& name, std::shared_ptr<ShaderConstantSet> const& pConstantSet) override;
		virtual void SetConstantSet(std::string const& name, ShaderConstantSetHandle const& constantSetHandle) override;
		virtual void SetTexture(std::string const& name
			, std::shared_ptr<GPUTexture> const& pTexture) override;
		virtual void SetTexture(std::string const& name
			, TextureHandle const& textureHandle) override;
		virtual void SetGPUBuffer(std::string const& name
			, GPUBufferHandle const& bufferHandle) override;
		virtual void SetSampler(std::string const& name
			, std::shared_ptr<TextureSampler> const& pSampler) override;
		virtual uint32_t GetBindingSetDescIndex() const override {
			return m_DescriptorIndex;
		}

		virtual std::unordered_map<std::string, std::shared_ptr<ShaderConstantSet>> const& GetExternalConstantSets() const override {
			return m_ExternalConstantSets;
		}

		virtual std::unordered_map<std::string, std::shared_ptr<GPUTexture>> const& GetExternalTextures() const override {
			return m_ExternalTextures;
		}

		virtual std::unordered_map<std::string, std::shared_ptr<TextureSampler>> const& GetExternalSamplers() const override {
			return m_ExternalSamplers;
		}

		virtual std::unordered_map<std::string, ShaderConstantSetHandle> const& GetInternalConstantSets() const override
		{
			return m_InternalConstantSets;
		}

		virtual std::unordered_map<std::string, TextureHandle> const& GetInternalTextures() const override {
			return m_InternalTextures;
		}

		virtual std::unordered_map<std::string, GPUBufferHandle> const& GetInternalGPUBuffers() const override {
			return m_InternalBuffers;
		}
	private:
		TIndex m_DescriptorIndex = INVALID_INDEX;
		std::unordered_map<std::string, std::shared_ptr<ShaderConstantSet>> m_ExternalConstantSets;
		std::unordered_map<std::string, std::shared_ptr<GPUTexture>> m_ExternalTextures;
		std::unordered_map<std::string, std::shared_ptr<TextureSampler>> m_ExternalSamplers;
		std::unordered_map<std::string, ShaderConstantSetHandle> m_InternalConstantSets;
		std::unordered_map<std::string, TextureHandle> m_InternalTextures;
		std::unordered_map<std::string, GPUBufferHandle> m_InternalBuffers;
	};
}