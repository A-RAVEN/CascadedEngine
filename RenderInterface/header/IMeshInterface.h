#pragma once
#include "CCommandList.h"
#include "Common.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "ShaderProvider.h"
#include "ShaderBindingBuilder.h"

namespace graphics_backend
{
	struct GraphicsPipelineStatesData
	{
		CPipelineStateObject pipelineStateObject;
		CVertexInputDescriptor vertexInputDescriptor;
		GraphicsShaderSet shaderSet;
		std::vector<ShaderBindingBuilder> shaderBindingDescriptors;

		bool operator==(GraphicsPipelineStatesData const& rhs) const
		{
			return pipelineStateObject == rhs.pipelineStateObject
				&& vertexInputDescriptor == rhs.vertexInputDescriptor
				&& shaderSet == rhs.shaderSet
				&& shaderBindingDescriptors == rhs.shaderBindingDescriptors;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, GraphicsPipelineStatesData const& rhs) noexcept
		{
			hash_append(h, rhs.pipelineStateObject);
			hash_append(h, rhs.vertexInputDescriptor);
			hash_append(h, rhs.shaderSet);
			hash_append(h, rhs.shaderBindingDescriptors);
		}
	};

	class IMeshInterface
	{
	public:
		virtual size_t GetGraphicsPipelineStatesCount() = 0;
		virtual GraphicsPipelineStatesData& GetGraphicsPipelineStatesData(size_t index) = 0;
		virtual size_t GetBatchCount() = 0;
		virtual void DrawBatch(size_t batchIndex, CInlineCommandList& commandList) = 0;
	};

	class IBatchManager
	{
	public:
		virtual TIndex RegisterGraphicsPipelineState(GraphicsPipelineStatesData const& pipelineStates);
		virtual void AddBatch(std::function<void(CInlineCommandList& commandList)> drawBatchFunc);
	};

	class IDrawBatchInterface
	{
	public:
		virtual void OnRegisterGraphicsPipelineStates(IBatchManager& batchManager);
	};
}