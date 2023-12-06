#pragma once
#include "TextureHandle.h"
#include "WindowHandle.h"
#include "ShaderBindingSetHandle.h"
#include "CNativeRenderPassInfo.h"
#include <functional>
#include <string>
#include <vector>
#include <type_traits>
#include "GPUBufferHandle.h"

namespace graphics_backend
{
	class CRenderpassBuilder;
	class ShaderConstantSet;
	class TextureSampler;
	class CRenderGraph;


	class CRenderGraph
	{
	public:
		//Used By Client
		virtual TextureHandle NewTextureHandle(GPUTextureDescriptor const& textureDesc) = 0;
		virtual GPUBufferHandle NewGPUBufferHandle(EBufferUsageFlags usageFlags
			, uint64_t count
			, uint64_t stride) = 0;
		virtual TextureHandle RegisterWindowBackbuffer(std::shared_ptr<WindowHandle> window) = 0;
		virtual CRenderpassBuilder& NewRenderPass(std::vector<CAttachmentInfo> const& inAttachmentInfo) = 0;
		virtual ShaderBindingSetHandle NewShaderBindingSetHandle(ShaderBindingBuilder const& builder) = 0;

		//Used By Backend
		virtual uint32_t GetRenderNodeCount() const = 0;
		virtual CRenderpassBuilder const& GetRenderPass(uint32_t nodeID) const = 0;
		virtual TextureHandle TextureHandleByIndex(TIndex index) const = 0;

		virtual TextureHandleInternalInfo const& GetTextureHandleInternalInfo(TIndex index) const = 0;
		virtual uint32_t GetTextureHandleCount() const = 0;
		virtual uint32_t GetTextureTypesDescriptorCount() const = 0;
		virtual GPUTextureDescriptor const& GetTextureDescriptor(TIndex descriptorIndex) const = 0;

		virtual IShaderBindingSetData* GetBindingSetData(TIndex bindingSetIndex) = 0;
		virtual IShaderBindingSetData const* GetBindingSetData(TIndex bindingSetIndex) const = 0;
		virtual uint32_t GetBindingSetDataCount() const = 0;
		virtual ShaderBindingBuilder const& GetShaderBindingSetDesc(TIndex descID) const = 0;

		virtual TIndex WindowHandleToTextureIndex(std::shared_ptr<WindowHandle> handle) const = 0;

		virtual std::unordered_map<WindowHandle*, TIndex> const& WindowHandleToTextureIndexMap() const = 0;
	};
}

#include "ShaderBindingSetHandleInl.h"
