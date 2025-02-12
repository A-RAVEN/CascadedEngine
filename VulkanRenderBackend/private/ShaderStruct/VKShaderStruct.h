#pragma once
#include <ShaderStruct.h>
#include <Compiler.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
	class VKShaderStruct : public ShaderStruct, public VKAppSubObjectBaseNoCopy
	{
	public:
		VKShaderStruct(CVulkanApplication& application, ShaderCompilerSlang::ShaderStructData const* pStructData);

		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) override;

		virtual void SetImage(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) override;

		virtual void SetBuffer(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) override;

		virtual void SetSampler(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) override;

		virtual void SetStruct(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) override;

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