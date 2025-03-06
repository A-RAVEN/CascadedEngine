#pragma once
#include <ShaderStruct.h>
#include <Compiler.h>
#include <Utils/D3D12SubobjectBase.h>

namespace graphics_backend
{

	class D3D2ShaderStruct : public ShaderStruct, public D3D12SubobjectBase
	{
	public:
		D3D2ShaderStruct(RenderBackend_D3D12* app, ShaderCompilerSlang::ShaderStructData const* pStructData);

		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) override;

		virtual void SetImageInternal(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) override;

		virtual void SetBufferInternal(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) override;

		virtual void SetSamplerInternal(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) override;

		virtual void SetStructInternal(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) override;

	public:
		castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<ShaderStruct>>> const& GetSubStructs() const { return m_NameToSubStructs; }
		castl::unordered_map<cacore::NameHash, castl::vector<castl::pair<ImageHandle, GPUTextureView>>> const& GetImageHandles() const { return m_NameToImageHandles; }
		castl::unordered_map<cacore::NameHash, castl::vector<TextureSamplerDescriptor>> const& GetSamplerDescriptors() const { return m_NameToSamplerDescriptors; }
		castl::unordered_map<cacore::NameHash, castl::vector<BufferHandle>> const& GetBufferHandles() const { return m_NameToBufferHandles; }
		castl::vector<uint8_t> const& GetSelfUniformBuffer() const { return m_SelfUniformBuffer; }
	private:
		ShaderCompilerSlang::ShaderStructData const* p_StructData;
		castl::vector<uint8_t> m_SelfUniformBuffer;
		castl::unordered_map<cacore::NameHash, uint32_t> m_NameToUniformElementMetaID;
		castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<ShaderStruct>>> m_NameToSubStructs;
		castl::unordered_map<cacore::NameHash, castl::vector<castl::pair<ImageHandle, GPUTextureView>>> m_NameToImageHandles;
		castl::unordered_map<cacore::NameHash, castl::vector<TextureSamplerDescriptor>> m_NameToSamplerDescriptors;
		castl::unordered_map<cacore::NameHash, castl::vector<BufferHandle>> m_NameToBufferHandles;
	};
}